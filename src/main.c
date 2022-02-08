#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include "libs/helpers.h"
#include "libs/structs.h"
#include "libs/macros.h"

// global variables.
_Atomic int threadCounter = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

void* acceptConn(void *arg) {
  acceptParams* ap = (acceptParams*) arg;

  int acceptedSocket;
  // extracts a request from the queue.
  if ((acceptedSocket = accept(ap->serverFd, ap->addr, ap->addrLen)) < 0) {
    perror("In accept");
    pthread_exit(NULL);
  }

  // deal with HTTP request.
  char reqBuf[HTTP_REQ_BUF];
  bzero(reqBuf, HTTP_REQ_BUF); 
  const size_t receivedBytes = read(acceptedSocket, reqBuf, HTTP_REQ_BUF);
  if (receivedBytes > 0) {
    char resBuf[HTTP_RES_BUF];

    // retrieve number from query.
    pthread_mutex_lock(&mutex);
    const int num = retrieveGETQueryIntValByKey(reqBuf, "num");
    pthread_mutex_unlock(&mutex);

    int fibResult = calFibonacci(num);
    sprintf(resBuf, "HTTP/1.1 200 OK\r\nContent-type: text/plain\r\nContent-length: %d\r\n\r\n%d", 
      calDigits(fibResult), fibResult);
    write(acceptedSocket, resBuf, strlen(resBuf));
  }
  close(acceptedSocket);
  atomic_fetch_sub(&threadCounter, 1);
  pthread_mutex_lock(&mutex);
  pthread_cond_signal(&cond);  // notify main thread.
  pthread_mutex_unlock(&mutex);
  return NULL;
}

int main(int argc, const char* argv[]) {
#if !defined(__STDC_NO_ATOMICS__)
  // initialize the server setup.
  serverSettings ss = { .thread_count = 4 };
  setupServerSettings(argc, argv, &ss);

  int serverFd; 
  sockaddr_in address;
  int addrLen = sizeof(address);
  
  // establish a socket.
  if ((serverFd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("In socket creation");
    exit(EXIT_FAILURE);
  }

  bzero(&address, addrLen); 
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;  // -> 0.0.0.0.
  address.sin_port = htons(PORT);
    
  // assigns specified address to the socket.
  if (bind(serverFd, (sockaddr*) &address, sizeof(address)) < 0) {
    perror("In bind");
    exit(EXIT_FAILURE);
  }

  // mark the socket as a passive socket.
  if (listen(serverFd, MAX_LISTEN_CONN) < 0) {
    perror("In listen");
    exit(EXIT_FAILURE);
  }
  printf("\nServer is now listening at port %d:\n\n", PORT);

  // main loop.
  while (1) {
    printf("Thread Count: %d\n", threadCounter);
    pthread_mutex_lock(&mutex);
    while (threadCounter >= ss.thread_count)
      pthread_cond_wait(&cond, &mutex);
    pthread_mutex_unlock(&mutex);

    // create new thread to handle the request.
    pthread_t thread_id;
    acceptParams ap = { serverFd, (sockaddr*) &address, (socklen_t*) &addrLen };
    pthread_create(&thread_id, NULL, acceptConn, &ap);
    atomic_fetch_add(&threadCounter, 1);
  }
#endif
  return EXIT_SUCCESS;
}