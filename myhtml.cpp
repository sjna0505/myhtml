/*********
	myhtml [options] -u --url <URL>
	-q --quiet	don't print response and each fetch result, showing only the total result
	-h --help	this help
	-p --profile	display statistical result of multiple fetches
 change log:
 	10/12/2020	v0.1 	the first working version
*********/
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <sstream>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <limits>
#include <cmath>
#include <map>
#include <vector>
#include <poll.h>
#include <getopt.h>
#include <algorithm>

#define PORT_NO 80
#define TIMEOUT 10
#define READ_BUFF 3000

using namespace std;
//default request headers
const string DEFAULT_AGENT = "User-Agent: Mozilla/4.0 (compatible; MSIE5.01; Windows NT)";
const string DEFAULT_ACCEPT= "Accept: */*";

//default strings to find at response headers and body
const string CONTENTLENGTH = "CONTENT-LENGTH: ";
const string HTMLTYPE = "CONTENT-TYPE: TEXT/HTML";
const string HTMLCLOSING = "</HTML>";
const string CHUNKENCODING = "TRANSFER-ENCODING: CHUNKED";

//from url input parse out protocol, hostname and path
map<string,string> html_parse(string url);

//set desination ip address and port number at the socket
char *html_ipaddr(char *host, int sock, sockaddr_in& dest);
//send html request
int html_send(int sock, sockaddr_in& dest, char* request);
//receive html response
string html_recv(int sock, int pkt_size); 

//from header string array, construct header map
map<string,string> compose_headers(vector<string> headerset);
//from header map, construct request string
string request_header(string host, string path, map<string,string> headers); 

//for case insensitive search, change a string to all upper cases
string upper(string s) { 
	for(unsigned int i=0;i<s.length();i++) s[i] = toupper(s[i]); 
	return s ;
}

void usage(char *command)
{
	cout<<command<<" [options] -u --url <URL>"<<endl;
	cout<<" -q --quiet	don't print response and each fetch result, showing only the total result"<<endl;
	cout<<" -h --help	this help"<<endl;
	cout<<" -p --profile	display statistical result of multiple fetches"<<endl;
}


int main(int argc, char* argv[]) {
	int quiet = 0;
	int count = 1;
	int stat = 0;
	struct timespec timestamp_s, timestamp_e, ttm_s, ttm_e;
	struct sockaddr_in dest;
	char * ipaddr = 0;
	double rtt_ms = 0.0;
	int receiv_cnt = 0, send_cnt = 0;
	int sock = 0 ;

	if(argc < 2)
	{
		usage(argv[0]);
		return 0;
	}
	int c;
	
	string url = "";
	string protocol = "",host_string = "",path = "/";
	while (1){
		static struct option long_options[] = {
			{"quiet", no_argument, 0, 'q'},
			{"help", no_argument, 0, 'h'},
			{"url", required_argument, 0, 'u'},
			{"profile", required_argument, 0, 'p'},
			{0,0,0,0}
		};
		int option_index = 0;
		c = getopt_long(argc, argv, "qhu:p:o",long_options,&option_index);
		if(c == -1) break;
		switch (c)
		{
			case 'q':
				quiet = 1;
				break;
			case 'h':
				usage(argv[0]);
				return 0;
			case 'u':
				url = optarg;
				break;
			case 'p':
				stat = 1;
				count = atoi(optarg);
				break;
			default:
				abort ();
		}
	}

	if( url == "") {
		usage(argv[0]);
		return 0;
	}

	map <string,string> request_url = html_parse(url);
	map <string,string>::iterator url_it;

	//if protocol is present, then only http is supported, default is also http
	url_it = request_url.find("protocol");
	if(url_it != request_url.end() and url_it->second != "http:"){
		cerr<<"Doesn't support protocol but http"<<endl;
		return 0;
	}
	//if there is no host parsed from url, then return error
	url_it = request_url.find("host");
	if(url_it == request_url.end()) {
		cerr<<"Invalid URL"<<endl;
		return 0;
	}
	host_string = url_it->second;

	//if path is parsed, then path is updated. default is "/"
	url_it = request_url.find("path");
	if(url_it != request_url.end()) path = url_it->second;

	//convert from string to char buff
	struct addrinfo *res = 0;
	char host[host_string.length() + 1];
	strcpy(host,host_string.c_str());
	getaddrinfo(host,NULL,NULL,&res);

	//variables to show the statistics, xsum, xxsum, min_rtt, max_rtt are initialized
	double xsum = 0.0;
	double xxsum = 0.0;
	double min_rtt = numeric_limits<double>::max(); 
	double max_rtt = 0.0;

	//construct default headerset and header map
	vector<string> headerset;
	headerset.push_back(DEFAULT_AGENT);
	headerset.push_back(DEFAULT_ACCEPT);
	map<string,string> headers = compose_headers(headerset);
	//from header map, request string is composed	
	string request = request_header(host_string,path,headers);
	//start timestamp of the whole batch of html transactions
	clock_gettime(CLOCK_MONOTONIC,&ttm_s);
	//rtt array to calculate median
	vector<double> rtts; 
	while(1)
	{
		int success = 1;
		int byte_sent = -1;

		//start timestamp for each html transcation 
		clock_gettime(CLOCK_MONOTONIC,&timestamp_s);
		//socket creation
		if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		{
			cerr<<"Socket creation error\n"<<endl;
			return 0;
		}
		//destination ip and port are set into the socket
		ipaddr = html_ipaddr(host,sock,dest);
		//if ip address is not resolved
		if(ipaddr == NULL)
		{
			cerr<<"invalid hostname or address "<<host<<endl;
			return 0;
		}
		//connect to the destination
		if(!quiet) cout<<"conneting to ... "<<host<<endl;
		if( connect(sock, (struct sockaddr *)&dest, sizeof(dest)) < 0)
		{
			cerr<<"Connection error to..."<<host<<endl;
			return 0;
		}
		char send_buf [request.length() + 1];
		//convert request string to char buff
		strcpy(send_buf,request.c_str());
		byte_sent = html_send(sock,dest,send_buf); 
		if(byte_sent < 0)
			success = 0;
		else{
			if(!quiet)
				cout<<"Request "<<byte_sent<<" bytes to "<<ipaddr<<"..."<<flush;
			send_cnt++;
		}
		string recv_string;
		//only if sending has succeeded, receiving happens
		//if not quiet mode, display the destination IP resolved and string converted from received bytes
		if(success) recv_string = html_recv(sock,READ_BUFF); 
		if(!quiet) cout<<recv_string<<endl;
		//receive status : received string == "" error
		if(recv_string.length() > 0){
			//end timestamp for each packet transaction
			clock_gettime(CLOCK_MONOTONIC,&timestamp_e);
			double elapsed = ((double)(timestamp_e.tv_nsec - timestamp_s.tv_nsec))/1000000.0;
			rtt_ms = (timestamp_e.tv_sec - timestamp_s.tv_sec)*1000.0 + elapsed;
			receiv_cnt++;
			xsum += rtt_ms; 
			xxsum += rtt_ms*rtt_ms; 
			min_rtt = min(rtt_ms,min_rtt);
			max_rtt = max(rtt_ms,max_rtt);
			rtts.push_back(rtt_ms);
		}
		//close socket
		shutdown(sock,2);
		//stop when number of requests exceed profile argument
		if(count and send_cnt >= count )
		{
			break;
		}
	}
	//end time check of all the requests
	clock_gettime(CLOCK_MONOTONIC,&ttm_e);
	double elapsed = ((double)(ttm_e.tv_nsec - ttm_s.tv_nsec))/1000000.0;
	rtt_ms = (ttm_e.tv_sec - ttm_s.tv_sec)*1000.0 + elapsed;
	//final statistics are printed if profile option is set
	if(send_cnt > 0 and stat) {
		cout<<"--- "<<host_string<<" html statistics ---"<<endl;
		cout<<send_cnt<<" requests sent, "<<receiv_cnt<<" responses received, "<<((send_cnt - receiv_cnt)*1.0/send_cnt)*100.0;
		cout<<"% request error, time "<<rtt_ms<<" ms"<<endl;
	}
	if (receiv_cnt > 0 and stat) {
		sort(rtts.begin(),rtts.end());

		cout<<"rtt min/avg/max/mdev/median = "<<min_rtt<<"/";
		cout<<xsum/receiv_cnt<<"/"<<max_rtt<<"/";
		cout<<sqrt(xxsum/receiv_cnt - (xsum/receiv_cnt)*(xsum/receiv_cnt))<<"/";
		int mid = int(rtts.size() / 2);
		if(rtts.size() % 2 == 0) cout<<(rtts[mid-1] + rtts[mid])/2;
		else cout<<rtts[mid];
		cout<<" ms"<<endl;
	}
	return 0;
}

map <string,string> html_parse(string url) {
	map <string,string> request_url;
	string starting = "";
	string path = "";
	int len = url.length();
	if (len > 0 ) {
		int k = 0;
		while(url[k] != '/' and k < len){
			starting += url[k];
			k++;
		}
		if( k == len){
			request_url.insert(make_pair("host",starting));
			return request_url;
		}
		if( starting == "http:" or starting == "https:"){
			request_url.insert(make_pair("protocol",starting));
			k++;
			if(url[k] != '/') {
				return request_url;
			}
			k++;
			starting = "";
			while(url[k] != '/' and k < len) {
				starting += url[k];
				k++;
			}
		}
		request_url.insert(make_pair("host",starting));
		if(k < len){
			while(k < len) {
				path += url[k];
				k++;
			}
			request_url.insert(make_pair("path",path));
		}
	}
	return request_url;
}

char* html_ipaddr(char *host, int sock, sockaddr_in& dest) {
	char *ipaddr = (char *)malloc(NI_MAXHOST*sizeof(char));

	struct hostent *host_entity;
	dest.sin_family = AF_INET;
	dest.sin_port = htons(PORT_NO);
	if( (host_entity = gethostbyname(host)) == NULL )
	{
		cerr<<"ip resolution fail for "<<host<<endl;
		return NULL;
	}
	strcpy(ipaddr, inet_ntoa( *(struct in_addr *) host_entity->h_addr));
	dest.sin_addr.s_addr = *(long*)host_entity->h_addr;
	return ipaddr;
}

map<string,string> compose_headers(vector<string> headerset) {
	map<string,string> headers;
	for(unsigned int i = 0; i < headerset.size(); i++) {
		int pos = headerset[i].find(": ");
		if(pos < int(headerset[i].length())-2){
			headers.insert(make_pair(headerset[i].substr(0,pos+1),headerset[i].substr(pos+2)));
		}
	}
	return headers;
}

string request_header(string host,string path,map<string,string> headers) {
	string request_string = "GET " + path + " HTTP/1.1\r\n";
	map<string,string>::iterator h_it;
	h_it = headers.find("Host:");
	if(h_it == headers.end()) request_string = request_string + "Host: " + host + "\r\n";
	for(h_it = headers.begin(); h_it != headers.end(); h_it++)
		request_string = request_string + h_it->first + " " + h_it->second + "\r\n";
	return request_string + "\r\n";
}

int html_send(int sock, sockaddr_in& dest, char* request) {

	int byte_written = send(sock,(char *)request, strlen(request), 0);
	if (byte_written <= 0) {
		cerr<<" (errno=" << strerror(errno) << " (" << errno << ")).";
		cerr <<"send fail"<<endl;
		return -1;
	}
	return byte_written;
}

string html_recv(int sock, int pkt_size) {
	stringstream  recv_stream;

	int sum = 0;
	int bytes = 0;
	int end_of_header = 0;
	int timeout = 1000000; //next read timeout 1sec
	int poll_timeout = 100; //poll timeout 1000ms
	string temp_str = "";
	struct timespec begin, now;
	int timediff;
	bool chunked = false;
	struct pollfd fds[1];
	memset(fds,0,sizeof(fds));
	fds[0].fd = sock;

	clock_gettime(CLOCK_MONOTONIC,&begin);
	char recv_buf[pkt_size];
	string last_string = "";
	while(1){
		int byte_read = 0;
		fds[0].events = POLLIN;
		memset(recv_buf,0,pkt_size);

		clock_gettime(CLOCK_MONOTONIC,&now);
		timediff = (int)(((double)(now.tv_nsec - begin.tv_nsec))/1000.0) + (now.tv_sec - begin.tv_sec)*1000000;

		if(poll(fds,1,poll_timeout) > 0){
			if(fds[0].revents & (POLLNVAL|POLLERR|POLLHUP)) break;
			if(fds[0].revents & POLLIN) {
				//poll there are data at the interface then read the socket
				byte_read = recv(sock, (char *)recv_buf, pkt_size,0);
			}
		}
		else {
		//poll, then no data. if socket read was OK then wait 1sec
		//there has been no socket read then wait 10sec
		//if chunked encoding, if </html> found, then break
			if(chunked){
				int pos = upper(last_string).find(HTMLCLOSING);
				if( pos > 0 and pos < int(last_string.length()) ){
					break;
				}
			}
			if(sum > 0 and timediff > timeout) {break;}
			else if(timediff > TIMEOUT*100000) {break;}
		}
		if(byte_read == 0) {
		}
		if(byte_read > 0) { 
			//new data came, then convert to string stream and store to the last_string
			sum += byte_read;
			last_string = "";
			for(int i = 0; i< byte_read; i++) {
				recv_stream << recv_buf[i];
				last_string += recv_buf[i];
			}
			clock_gettime(CLOCK_MONOTONIC,&begin);
		}
		//response header is parsed 
		//if content-length is set, then we can find the break condition without waiting timeout
		//if chunked encoding is set, then break when </html> is seen
		if(sum > 0 and temp_str == "") {
			temp_str = recv_stream.str();
			//find content-length header and calculate the content-length
			int pos = upper(temp_str).find(CONTENTLENGTH) + CONTENTLENGTH.length();
			while(temp_str[pos] >= '0' and temp_str[pos] <= '9') {
				bytes = bytes*10 + (temp_str[pos] - '0'); 
				pos++;
			}
			//if content-length is calculated, need to know the header length
			if(bytes > 0) {
				int check = 0;
				while(pos < pkt_size){
					if(recv_buf[pos] == '\r' and check % 2 == 0) check++;
					if(recv_buf[pos] == '\n' and check == 1) check++;
					if(recv_buf[pos] == '\n' and check == 3){
						end_of_header = pos + 1;
						break;
					}
					pos++;
				}
			}
			//if chunk-encoding is set
			if( upper(temp_str).find(HTMLTYPE) > 0) 
				if( upper(temp_str).find(CHUNKENCODING) > 0) chunked = true;
		}
		//break condition, when total received bytes > content-length + header size
		if(bytes > 0 and end_of_header > 0 and sum >= (bytes + end_of_header)) {
			break;
		}
	}
	if(sum <= 0) {
		cerr<<" (errno=" << strerror(errno) << " (" << errno << ")).";
		cerr << "receive error"<<endl;
		return "";
	}
	return recv_stream.str();
}
