all: myhtml

myhtml: myhtml.cpp
	g++ -g -Wall myhtml.cpp -o myhtml

clean:
	rm myhtml
