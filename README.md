<h1>Multi Threaded Proxy Server with and without Cache</h1>

This project is implemented using `C++` and Parsing of HTTP referred from <a href = "https://github.com/nekipelov/httpparser"> Proxy Server </a>

## Project Theory


##### Motivation/Need of Project

- To Understand →
  - The working of requests from our local computer to the server.
  - The handling of multiple client requests from various clients.
  - Locking procedure for concurrency.
  - The concept of cache and its different functions that might be used by browsers.
- Proxy Server do →
  - It speeds up the process and reduces the traffic on the server side.
  - It can be used to restrict user from accessing specific websites.
  - A good proxy will change the IP such that the server wouldn’t know about the client who sent the request.
  - Changes can be made in Proxy to encrypt the requests, to stop anyone accessing the request illegally from your client.

  ##### OS Component Used ​

- Threading
- Locks
- Semaphore
- Cache (LRU algorithm is used in it)

# Note:

- This code can only be run in Linux Machine. Please disable your browser cache.

## How to Run

- Open This Project folder in terminal,
  then write given commands
  ```bash
     $ make all
     $ ./main
  ```

## Output
![alt text](<Screenshot from 2024-03-30 23-54-06.png>)

2.Debugging (printing http request)

![alt text](<Screenshot from 2024-03-30 23-49-18.png>)