#include "proxy.hpp"

#include <unistd.h>

#include <cassert>
#include <memory>
#include <thread>

#include "log.hpp"
#include "utils.hpp"

#define PORT "8086"            //listen port
#define MAXPENDING 10           //pending request
#define MAXCachingCapacity 500  //cache size

long requestID = 0;
// pthread_mutex_t logLock = PTHREAD_MUTEX_INITIALIZER;
// pthread_mutex_t cacheLock = PTHREAD_MUTEX_INITIALIZER;
Cache * myCache = new Cache(MAXCachingCapacity);

/**
 * @func: start the listen process in the main thread
 *  create proxy thread for all incoming request on PORT
 * @param: {void}  
 * @return: {void} 
 * @ref:https://beej.us/guide/bgnet/html//index.html
 */
void proxyListen() {
  int status;
  int socket_fd;
  struct addrinfo host_info;
  struct addrinfo *host_info_list, *a;
  const char * hostname = "0.0.0.0";
  const char * port = PORT;

  memset(&host_info, 0, sizeof(host_info));

  host_info.ai_family = AF_UNSPEC;
  host_info.ai_socktype = SOCK_STREAM;
  host_info.ai_flags = AI_PASSIVE;

  //get addr info
  status = getaddrinfo(hostname, port, &host_info, &host_info_list);
  if (status != 0) {
    log(std::string("(no-id): ERROR cannot get address info for host \n"));
    return;
  }

  //bind socket
  for (a = host_info_list; a != NULL; a = a->ai_next) {
    socket_fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (socket_fd == -1) {
      log(std::string("(no-id): ERROR cannot create socket \n"));
      continue;
    }
    int yes = 1;
    status = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
    status = bind(socket_fd, a->ai_addr, a->ai_addrlen);
    if (status == -1) {
      close(socket_fd);
      log(std::string("(no-id): ERROR cannot bind socket \n"));
      continue;
    }
    break;
  }
  if (a == NULL) {
    log(std::string("(no-id): ERROR selectserver failed to bind \n"));
    exit(EXIT_FAILURE);
  }

  // listen on socket
  freeaddrinfo(host_info_list);
  status = listen(socket_fd, MAXPENDING);
  if (status == -1) {
    log(std::string("(no-id): ERROR cannot listen on socket \n"));
    return;
  }

  cout << "Waiting for connection on port " << port << endl;
  struct sockaddr_in socket_addr;
  socklen_t socket_addr_len = sizeof(socket_addr);
  int client_connection_fd;
  while (1) {
    client_connection_fd =
        accept(socket_fd, (struct sockaddr *)&socket_addr, &socket_addr_len);
    if (client_connection_fd == -1) {
      log("(no-id): Failed to estblish connection with client");
    }
    else {
      string ip = inet_ntoa(socket_addr.sin_addr);
     
      // pthread_t thread;
      std::unique_ptr<Proxy> myProxy(new Proxy(requestID, client_connection_fd, ip));
       // **Print the received HTTP request**
      // std::vector<char> data_buff = recvBuff(myProxy->return_socket_des());
      // std::string request(data_buff.begin(), data_buff.end());
      // cout << "**Received HTTP Request:**" << endl;
      // cout << request << endl;
///
      std::thread proxythread(runProxy1, std::move(myProxy));
      proxythread.detach();
      requestID++;
    }
  }

  return;
}
/**
 * @func: start proxy. 
 * @param: {Proxy pointer include UID, socket, ip}  
 * @return: {void} 
 */
void * runProxy1(std::unique_ptr<Proxy> myProxy) {
  std::unique_ptr<Proxy> Proxy_instance(std::move(myProxy));
  try {
    // receive request
    std::vector<char> data_buff = recvBuff(Proxy_instance->return_socket_des());
    std::string Line(data_buff.begin(), data_buff.end());
    //  cout << "**Received HTTP Request:**" << endl;
    //   cout << Line << endl;
    Proxy_instance->setRequest(Line, data_buff);
    Proxy_instance->judgeRequest();
    // delete Proxy_instance;
  }
  catch (std::exception & e) {
    // delete Proxy_instance;
    return NULL;
  }
  return NULL;
}

/**
 * @func: start proxy. 
 * @param: {Proxy pointer include UID, socket, ip}  
 * @return: {void} 
 */
void * runProxy(void * myProxy) {
  Proxy * Proxy_instance = (Proxy *)myProxy;
  try {
    // receive request
    std::vector<char> data_buff(65536, 0);
    int data_rec = 0;
    data_rec = recv(Proxy_instance->return_socket_des(), &data_buff.data()[0], 65536, 0);
    if (data_rec <= 0) {
      std::cerr << "cannot receive request" << std::endl;
      log(std::string(to_string(Proxy_instance->uid)) +
          ": ERROR cannot receive the request from " + Proxy_instance->clientIP);
      throw std::exception();
    }
    data_buff.resize(data_rec);

    std::string Line(data_buff.begin(), data_buff.end());
    Proxy_instance->setRequest(Line, data_buff);
    Proxy_instance->judgeRequest();
    delete Proxy_instance;
  }
  catch (std::exception & e) {
    delete Proxy_instance;
    std::cerr << e.what() << std::endl;
  }
  return NULL;
}

/**
 * @func: parse the request and make it the object
 * @param: {line_send a char vector of the request}  
 * @return: {void} 
 */
void Proxy::setRequest(std::string Line, std::vector<char> & line_send) {
  int error;
  time_t curr_time;
  time(&curr_time);
  std::unique_ptr<http_Request> request_new(
      new http_Request(this->socket_des, Line, line_send, this->clientIP, curr_time));
  request = std::move(request_new);
  error = request->constructRequest();

  //if pasrse failed, respond with error 400
  if (error == -1) {
    // delete request;
    if (line_send.size() > 4) {
      std::cerr << "Request construction failed" << std::endl;
      log(std::string(to_string(uid) + ": Warning invalid request received \n"));
    }
    proxyERROR(400);
    throw std::exception();
  }

  std::string msg = to_string(uid) + ": \"" + request->return_request() + "\" from " +
                    request->return_ip() + " @ " + parseTime(request->return_time());
  log(msg);
}

/**
 * @func: judge the request by method,
 *  call different proxy function based on method
 * @param: {void}  
 * @return: {void} 
 */
void Proxy::judgeRequest() {
  if (request->return_method() == "CONNECT") {
    std::cout << "Start CONNECT Proxy" << std::endl;
    proxyCONNECT();
    log(std::string(to_string(uid) + ": Tunnel closed \n"));
  }
  else if (request->return_method() == "POST") {
    std::cout << "Start POST Proxy" << std::endl;
    proxyPOST();
  }
  else if (request->return_method() == "GET") {
    std::cout << "Start GET Proxy" << std::endl;
    proxyGET();
  }
  else {
    //Bad Request
    proxyERROR(400);
  }
}

/**
 * @func: connect the host server
 * @param: {void}  
 * @return: {Success: socket of the server. Fail: exit thread} 
 */
int Proxy::connectServer() {
  int error;
  int socket_server;
  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  //get server address info
  error = getaddrinfo(
      request->return_Host().c_str(), request->return_port().c_str(), &hints, &res);
  if (error != 0) {
    log(std::string(to_string(uid) +
                    ": ERROR cannot get the address info from the host \n"));
    proxyERROR(404);
    throw std::exception();
  }

  //create socket
  socket_server = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (socket_server == -1) {
    log(std::string(to_string(uid) + ": ERROR cannot create socket for host" + "\n"));
    proxyERROR(404);
    throw std::exception();
  }

  //connect the server
  error = connect(socket_server, res->ai_addr, res->ai_addrlen);
  if (error == -1) {
    log(std::string(to_string(uid) + ": ERROR cannot connect to the socket \n"));
    proxyERROR(404);
    throw std::exception();
  }

  freeaddrinfo(res);
  return socket_server;
}

/**
 * @func: 1. Setup the connection with target server
 *        2. Reply a HTTP 200 OK 
 *        3. Connect the Tunnel
 * @param: {void}
 * @return: {void}
 */
void Proxy::proxyCONNECT() {
  int socket_server = connectServer();
  this->server_des = socket_server;
  fd_set listen_ports;
  int socket_client = this->return_socket_des();
  int error;
  error = send(socket_client, "HTTP/1.1 200 OK\r\n\r\n", 19, 0);
  if (error < 0) {
    cerr << "Cannot send back" << std::endl;
    log(std::string(to_string(uid) + ": ERROR cannot send back to client \n"));
    return;
  }

  log(std::string(to_string(uid) + ": Responding \"HTTP/1.1 200 OK\" \n"));

  // start tunnels
  int maxdes = socket_client > socket_server ? socket_client : socket_server;
  int end = 1;
  while (end) {
    FD_ZERO(&listen_ports);
    FD_SET(socket_client, &listen_ports);
    FD_SET(socket_server, &listen_ports);

    error = select(maxdes + 1, &listen_ports, NULL, NULL, NULL);
    if (error == -1) {
      std::cerr << "Error cannot select" << std::endl;
      log(std::string(to_string(uid) + ": ERROR cannot select \n"));
      throw std::exception();
    }

    for (int i = 0; i <= maxdes; i++) {
      if (FD_ISSET(i, &listen_ports)) {
        //There is a message received
        std::vector<char> data_buff = recvChar(i);
        int length = data_buff.size();
        if (data_buff.size() == 0) {
          FD_ZERO(&listen_ports);
          end = 0;
          return;
        }
        else {
          if (i == socket_server) {
            //The message is from server
            sendall(socket_client, &data_buff.data()[0], &length);
          }
          else {
            //The message is from client
            sendall(socket_server, &data_buff.data()[0], &length);
          }
        }  //End of if for recv 0
      }    //End of if for check fd_set
    }      //End of go over the potential descriptor
  }        //main while loop
  return;  //End of func
}

/**
 * @func: 1. froward the request to server
 *        2. receive response
 *        3. forward response to client
 * @param: {void}  
 * @return: {void} 
 */
void Proxy::proxyPOST() {
  int socket_server = connectServer();
  this->server_des = socket_server;
  int socket_client = socket_des;
  std::vector<char> send_request = request->return_line_send();
  http_Response * Proxy_temp = proxyFetch(socket_server, socket_client);
  if (Proxy_temp != NULL) {
    delete Proxy_temp;
  }
}

/**
 * @func: 1. Search from cache
 *        2. Check Expire and validation
 *        3. Send validation to server if needed
 *        4. If good: send cached response back
 *              else: send the request, and get response update and reply with the response
 * @param: {void}  
 * @return: {void} 
 */
void Proxy::proxyGET() {
  int socket_client = socket_des;
  std::string request_url = request->return_uri();
  //search from the cache
  http_Response * response_instance = myCache->get(request_url);
  if (response_instance == NULL) {
    //no response in cache
    std::cout << "not in cache" << std::endl;
    log(std::string(to_string(uid) + ": not in cache\n"));
    int socket_server = connectServer();
    this->server_des = socket_server;
    response_instance = proxyFetch(socket_server, socket_client);
    if (response_instance && response_instance->return_statuscode() == 200) {
      receiveLog(response_instance);
      //if the response is 200 we need to cache it
      if(response_instance->return_no_store() == false){
        std::string removed_node = myCache->put(request_url, response_instance);
        if (removed_node.size() != 0) {
          //There is a node being removed, need to log
          log(std::string("(no-id): NOTE evicted" + removed_node + "from cache\n"));
        }
      }
    }
    //No need to cache
    return;
  }
  else {
    //If we find this in the cache;
    std::cout << "Find in cache" << std::endl;
    //Expiration function
    time_t request_max_age = request->return_max();
    bool if_expire = myCache->checkExpire(request_url, request_max_age);
    if (if_expire) {
      //Expired cached, need update
      std::cout << "expire cache" << std::endl;
      if (response_instance->return_expire() != 0) {
        log(std::string(to_string(uid) + ": in cache, but expired at " +
                        parseTime(response_instance->return_expire())));
      }
      else {
        time_t minMaxAge;
        time_t maxAge = response_instance->return_max();
        if (maxAge != -1 && request_max_age != -1) {
          minMaxAge = maxAge > request_max_age ? request_max_age : maxAge;
        }
        else {
          minMaxAge = maxAge < request_max_age ? request_max_age : maxAge;
        }
        log(std::string(to_string(uid) + ": in cache, but expired at " +
                        parseTime(response_instance->return_date() + minMaxAge) + "\n"));
      }
      HandleValidation(response_instance, request_url);
      return;
    }
    else {
      time_t freshness = request->calFresh();
      if (freshness)
        //Valid, but need validation
        std::cout << "cache not expired" << std::endl;
      bool if_no_cache_request = request->return_no_cache();
      bool if_no_cache_response = response_instance->return_no_cache();
      //request or response: no-cache or must-revalidate
      if (if_no_cache_request || if_no_cache_response ||
          (response_instance->return_max() + freshness +
               response_instance->return_date() >
           time(0))) {
        std::cout << "Need validation" << std::endl;
        log(std::string(to_string(uid) + ": in cache, requires validation\n"));
        HandleValidation(response_instance, request_url);
        return;
      }
      else {
        //no validation required
        std::cout << "Don't need validation" << std::endl;
        log(std::string(to_string(uid) + ": in cache, valid\n"));
        std::vector<char> reply = response_instance->return_line_recv();
        int length = reply.size();
        sendall(socket_client, &reply.data()[0], &length);
        // close(socket_client);-------
        return;
      }
    }
  }
}

/**
 * @func: forward the request, fetch the response, send back to client
 * @param: {socket_server, socket_client}  
 * @return: {Success: a valid response pointer. Fail: Null} 
 */
http_Response * Proxy::proxyFetch(int socket_server, int socket_client) {
  std::vector<char> send_request = request->return_line_send();
  http_Response * r1 = NULL;
  int length = send_request.size();

  int error = sendall(socket_server, &send_request.data()[0], &length);
  if (error == 0) {
    log(std::string(to_string(uid) + ": Requesting \"" + request->return_request() +
                    "\" from " + request->return_Host() + "\n"));

    std::vector<char> input = recvBuff(socket_server);
    std::string str(input.begin(), input.end());
    size_t resp_pos = str.find("\r\n");
    std::string respStr = str.substr(0, resp_pos);
    log(std::string(to_string(uid) + ": Received \"" + respStr + "\" from " +
                    request->return_Host() + "\n"));

    if (check502(input)) {
      proxyERROR(502);
      throw std::exception();
    }

    http_Response * chunkResp = chunkHandle(input, socket_server);
    if (chunkResp != NULL) {
      return chunkResp;
    }

    if (input.size() != 0) {
      int length = input.size();
      if (sendall(socket_client, &input.data()[0], &length) == 0) {
        std::string reply(input.begin(), input.end());
        r1 = new http_Response(socket_server, reply, input);
        if (r1->parseResponse(input) == -1) {
          //error in constructing Reponse;
          return NULL;
        }
        else {
          log(std::string(to_string(uid) + ": Responding \"" + r1->return_response() +
                          "\"\n"));
          return r1;
        }
      }
    }
  }
  return r1;
}

/**
 * @func: error case of the proxy, send the client with error
 * @param: {code: error code}  
 * @return: {void} 
 */
void Proxy::proxyERROR(int code) {
  int client_fd = socket_des;
  const char * resp;
  switch (code) {
    case 404:
      resp = "HTTP/1.1 404 Not Found\r\n\r\n";
      break;
    case 400:
      resp = "HTTP/1.1 400 Bad Request\r\n\r\n";
      break;
    case 502:
      resp = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
      break;
  }
  string logLine = resp;
  send(client_fd, resp, strlen(resp), 0);
  log(std::string(to_string(uid) + ": Responding \"" +
                  logLine.substr(0, logLine.size() - 4) + "\"\n"));
}

/**
 * @func: handle the chunk response. keep receiving and forwarding if the response is chunked
 * @param: {input: response message, server_fd: server socket}  
 * @return: {True for chunk resp and False for normal} 
 */
http_Response * Proxy::chunkHandle(vector<char> & input, int server_fd) {
  std::string str(input.begin(), input.end());
  http_Response * resp = NULL;
  //parse response line
  size_t resp_pos = str.find("\r\n");
  std::string respStr = str.substr(0, resp_pos);

  //check if it is a chunk response
  size_t start = str.find("Transfer-Encoding:");
  if (start == std::string::npos) {
    return resp;
  }
  size_t end = str.find("\n", start);
  std::string encodeLine = str.substr(start, end);
  if (encodeLine.find("chunked") == std::string::npos) {
    return resp;
  }

  vector<char> total(input);

  // send response
  int length = input.size();
  sendall(socket_des, &input.data()[0], &length);
  log(std::string(to_string(uid) + ": Responding \"" + respStr + "\"\n"));

  //keep receiving and forwarding
  while (1) {
    std::vector<char> chunkMsg = recvChar(server_fd);
    if (chunkMsg.size() <= 0) {
      break;
    }
    total.insert(total.end(), chunkMsg.begin(), chunkMsg.end());
    int length = chunkMsg.size();
    sendall(socket_des, &chunkMsg.data()[0], &length);
  }
  std::string whole_resp(total.begin(), total.end());
  resp = new http_Response(server_fd, whole_resp, total);
  int error = resp->parseResponse(total);
  if (error == -1) {
    std::cout << "chunk parse failed" << std::endl;
    return NULL;
  }
  return resp;
}

/**
 * @func: check the response to see whether it have a line
 * @param: {response message}  
 * @return: {True: no 502 error, False: 502 error} 
 */
bool Proxy::check502(vector<char> & input) {
  std::string str(input.begin(), input.end());
  size_t resp_pos = str.find("\r\n\r\n");
  if (resp_pos != std::string::npos) {
    return false;
  }
  return true;
}

/**
 * @func: make a validation request used to send to the server
 * @param: {response_instance: cached response pointer}  
 * @return: {response message for validation} 
 */
std::vector<char> Proxy::ConstructValidation(http_Response * response_instance) {
  std::string request_line = request->return_request();
  request_line += "\r\n";
  request_line += "Host:";
  request_line += request->return_host_line();
  request_line += "\r\n";

  string last_modify_str = response_instance->return_last_str();
  if (last_modify_str.size() != 0) {
    request_line += "If-Modified-Since: ";
    request_line += last_modify_str;
    request_line += "\r\n";
  }
  std::string etags_response = response_instance->return_etags();
  if (etags_response.size() != 0) {
    request_line += "If-None-Match: ";
    request_line += etags_response;
    request_line += "\r\n";
  }
  request_line += "\r\n";

  if (last_modify_str.size() != 0 || etags_response.size() != 0) {
    std::vector<char> reply(request_line.begin(), request_line.end());
    return reply;
  }
  else {
    //shouldn't miss two fields
    std::vector<char> reply = request->return_line_send();
    return reply;
  }
}

/**
 * @func: handle the validation process.
 *        1. check whether need to valid
 *        2. send to server if needed
 *        3. check whether need to refetch
 *        4. refetch and return if needed
 * @param: {response, request url}  
 * @return: {void} 
 */
void Proxy::HandleValidation(http_Response * response_instance, std::string request_url) {
  int socket_client = socket_des;
  assert(response_instance != NULL);

  //resend the request, cache update
  int socket_server = connectServer();
  this->server_des = socket_server;
  std::vector<char> revalid_request = ConstructValidation(response_instance);
  int length = revalid_request.size();
  //log requesting
  log(std::string(to_string(uid) + ": Requesting \"" + request->return_request() +
                  "\" from " + request->return_Host() + "\n"));
  sendall(socket_server, &revalid_request.data()[0], &length);
  //reply with the new response
  std::vector<char> reply = recvBuff(socket_server);
  std::string str(reply.begin(), reply.end());
  size_t resp_pos = str.find("\r\n");
  std::string respStr = str.substr(0, resp_pos);
  //log receving
  log(std::string(to_string(uid) + ": Received \"" + respStr + "\" from " +
                  request->return_Host() + "\n"));
  std::string reply_str(reply.begin(), reply.end());
  http_Response * new_response = new http_Response(socket_server, reply_str, reply);
  int error = new_response->parseResponse(reply);
  if (error == -1) {
    //error in constructing Reponse;
    //502 error code
    std::cerr << "Reponse parsing fail" << std::endl;
  }
  else {
    std::cout << "Responding " << new_response->return_response() << std::endl;
    log(std::string(to_string(uid) + ": Responding \"" + new_response->return_response() +
                    "\"\n"));
    if (new_response->return_statuscode() == 200) {
      //update and return to client
      std::string removed_url = myCache->put(request_url, new_response);
      if (removed_url.size() != 0) {
        log(std::string("(no-id): NOTE evicted" + removed_url + " from cache \r\n"));
      }
      int length = reply.size();
      sendall(socket_client, &reply.data()[0], &length);
    }
    else if (new_response->return_statuscode() == 304) {
      //not modified return the cached response
      std::vector<char> cached_response = response_instance->return_line_recv();
      int length = cached_response.size();
      sendall(socket_client, &cached_response.data()[0], &length);
    }
    else {  //5xx
      int length = reply.size();
      sendall(socket_client, &reply.data()[0], &length);
    }
  }
}

/**
 * @func: send all data
 * @param: {s: destination socket, buf: message, len, length of message}  
 * @return: {0 for success and -1 for fail} 
 * @ref: https://beej.us/guide/bgnet/html/index.html#recvman
 */
int Proxy::sendall(int s, char * buf, int * len) {
  int total = 0;         // how many bytes we've sent
  int bytesleft = *len;  // how many we have left to send
  int n;

  while (total < *len) {
    n = send(s, buf + total, bytesleft, 0);
    if (n == -1) {
      break;
    }
    total += n;
    bytesleft -= n;
  }

  *len = total;             // return number actually sent here
  return n == -1 ? -1 : 0;  // return -1 on failure, 0 on success
}

/**
 * @func: Log the GET 200 OK response at first received time
 * @param: {response}  
 * @return: {void} 
 */
void Proxy::receiveLog(http_Response * resp) {
  std::string cacheControl = resp->return_cache_ctrl();
  if (resp->return_etags().size() != 0) {
    log(std::string(to_string(uid) + ": Note Etags: " + resp->return_etags() + "\n"));
  }
  if (cacheControl != "") {
    log(std::string(to_string(uid) + ": Note Cache-Control: " + cacheControl + "\n"));
  }
  std::string reason = resp->return_no_cache_reason();
  if (reason.size() != 0) {
    log(std::string(to_string(uid) + ": not cacheable because " + reason + "\n"));
  }
  else if (resp->return_no_cache()) {
    log(std::string(to_string(uid) + ": cached, but requires re-validation \n"));
  }
  else {
    time_t max_age = resp->return_max();
    time_t expireTime = resp->return_expire();
    time_t recvTime = resp->return_date();
    if (max_age == -1 && expireTime == 0) {
      //no expire
      log(std::string(to_string(uid) + ": cached, expires at Not Declared\n"));
    }
    else if (max_age != -1) {
      log(std::string(to_string(uid) + ": cached, expires at " + parseTime(recvTime + max_age)));
    }
    else {
      log(std::string(to_string(uid) + ": cached, expires at " + parseTime(expireTime)));
    }
  }
}