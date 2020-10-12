# myhtml
for system engineering practice, CLI based html request and response without HTML library

myhtml.cpp 

compile and execution:
make

./myhtml [options] -u --url <URL>
	-q --quiet      don't print response and each fetch result, showing only the total result
	-h --help       help
	-p --profile    display statistical result of multiple fetches

program structure:
1. parse the url input 
2. based on url and default headers, construct html request
3. prepare socket
4. connect
5. send request
6. receive response
7. show the results
I put the loop of calling 3,4,5 and 6. showing into main driver function.

notes for receive:
the receiving loop escapes when for a certain while there is no data to read by polling.
So if the sender is done with sending, the receiver should wait for the pre-defined timeout.
I added two conditions for earlier escapes,
1. at the response header, if "Content-Length: <length>" is found, then when the total bytes is exceeding the header size plus the content-length, the loop can be escaped
2. at the response header, if "Transfer-Encoding: Chunked" is found, then when "</html>" is found then html document is ended and we can escape
