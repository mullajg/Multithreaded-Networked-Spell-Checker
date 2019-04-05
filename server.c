#include <stdio.h>
#include <stdlib.h>
#include <string.h> //strlen
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr
#include <unistd.h> //write
#include <pthread.h> //thread creation
#include <semaphore.h>
#define NUMWORKERS 5
#define BUFSIZE 10
//Try using pthread mutex lock and condition variables instead of sem_t
//pthread_mutex_t and pthread_mutex_lock(&name)
//p_thread_cond_t()
typedef struct{
    int buf[BUFSIZE];           //Buffer array
    int items;                  //Number of occupied slots
    int in;                     //insert index
    int out;                    //remove index
    pthread_mutex_t mutex;      //protects accesses to buf
    pthread_cond_t itemAvail;   //Counts available slots
    pthread_cond_t slotAvail;   //Counts available items
} workBuffer;

typedef struct{
    char** buf;  //Buffer array
    int n;       //Maximum number of slots
    int front;   //buf[(front + 1)%n] is first item
    int rear;    //buf[rear%n] is last item
    pthread_mutex_t mutex; //protects accesses to buf
    sem_t slots; //Counts available slots
    sem_t items; //Counts available items
} logBuffer;


void workBuffer_insert(workBuffer*, int);
int workBuffer_remove(workBuffer*);

void logBuffer_init(logBuffer*, int);
void logBuffer_insert(logBuffer*, char*);
char* logBuffer_remove(logBuffer*);

void mainThread(char* argv[]);
void createThreadPool();
void *workerThread();
void workHandler(int);
void *serverLogThread();
char* concat(char*, int, int);

FILE *fp;
FILE *wp;
workBuffer workQueue;
logBuffer logQueue;
pthread_t workerThreadId[5];
pthread_t logThread;
sem_t catMutex;
sem_t writeMutex;


///////////////////Work Buffer Methods////////////////////////////////
//Insert item onto the rear of shared buffer sp
void workBuffer_insert(workBuffer *bp, int item){
    pthread_mutex_lock(&bp->mutex);         //Hold lock
    while(bp->items >= BUFSIZE){            //While the buffer is full
        pthread_cond_wait(&bp->slotAvail, &bp->mutex); //Wait for an available slot
    }
    bp->buf[bp->in++] = item;               //Insert item into array at in (increment in)
    bp->in %= BUFSIZE;                      //This line keeps the buffer bounded
    bp->items++;                            //Increase number of items
    pthread_cond_signal(&bp->itemAvail);    //Signal that there is an available item
    pthread_mutex_unlock(&bp->mutex);       //Unlock mutex
 }

//Remove and return the first item form buffer bp
int workBuffer_remove(workBuffer *bp){
    int item;                               //Return value
    pthread_mutex_lock(&bp->mutex);         //Lock mutex
    while(bp->items <= 0){                  //While there are no items in buffer
        pthread_cond_wait(&bp->itemAvail, &bp->mutex); //Wait for an available item
    }
    item = bp->buf[bp->out++];              //Remove item from buffer at location out (increment out)
    bp->out %= BUFSIZE;                     //This line keeps the buffer bound
    bp->items--;                            //Decrement items
    pthread_cond_signal(&bp->slotAvail);    //Signal that there is an available slot
    pthread_mutex_unlock(&bp->mutex);       //Unlock mutex
    return item;
}
///////////////////////////////////////////////////////////////////////////




//////////////////Log Buffer Methods///////////////////////////////////////
void logBuffer_init(logBuffer *bp, int n){
    bp->buf = calloc(n, sizeof(char) * 256);
    bp->n = n;                  //Buffer holds max of n items
    bp->front = bp->rear = 0;   //Empty buffer if front == rear
    pthread_mutex_unlock(&bp->mutex); //Binary semaphore for locking
    sem_init(&bp->slots, 0, n); //Initially, buf has n empty slots
    sem_init(&bp->items, 0, 0); //Initially, buf has 0 data items
}

//Insert item onto the rear of shared buffer bp
void logBuffer_insert(logBuffer *bp, char* item){
    sem_wait(&bp->slots);                        //Wait for available slot
    pthread_mutex_lock(&bp->mutex);              //Lock the buffer
    bp->buf[(++bp->rear)%(bp->n)] = malloc(sizeof(char) * 256);
    strcpy(bp->buf[(bp->rear)%(bp->n)], item);   //Insert the item
    pthread_mutex_unlock(&bp->mutex);            //Unlcok the buffer
    sem_post(&bp->items);                        //Announce availbe item
}

//Remove and return the first item form buffer bp
char* logBuffer_remove(logBuffer *bp){
    char* item;
    sem_wait(&bp->items);                   //Wait for available item
    pthread_mutex_lock(&bp->mutex);         //Lock the buffer
    item = bp->buf[(++bp->front)%(bp->n)];  //Remove the item
    pthread_mutex_unlock(&bp->mutex);       //Unlock the buffer
    sem_post(&bp->slots);                   //Announce available slot
    return item;
}
////////////////////////////////////////////////////////////////////////

void error(const char *msg){
    perror(msg);
    exit(1);
}

int main(int argc, char* argv[]){
    if(argc < 2){ //Ensuring that the user provides the port number in command line
        fprintf(stderr, "Port number not provided. Program terminated");
        exit(1);
    }
    else if(argc == 3){
        fp = fopen(argv[2], "r");
    }
    else{
        fp = fopen("words", "r");
    }
    wp = fopen("log", "w");
    fprintf(wp, "Word | OK/MISSPELLED | Client Number\n");
    fprintf(wp, "------------------------------------\n");
    sem_init(&catMutex, 0, 1);
    sem_init(&writeMutex, 0, 1);
    logBuffer_init(&logQueue, 5);
    createThreadPool();
    mainThread(argv);

    return 0;
}

void createThreadPool(){
    for(size_t i = 0; i < 5; ++i){
        pthread_create(&workerThreadId[i], NULL, workerThread, NULL);
    }
    pthread_create(&logThread, NULL, serverLogThread, NULL);
}

void mainThread(char* argv[]){
    int sockfd, newSockfd, portNo, n, *newSock;
    char buffer[256];
    struct sockaddr_in serv_addr, cli_addr; //Structure that will hold our server and client address
    socklen_t clilen; //length of client address

    sockfd = socket(AF_INET, SOCK_STREAM, 0); //Creating our socket
    if(sockfd < 0){ //Error checking socket creation
        error("Error opening socket.");
    }

    bzero((char *)&serv_addr, sizeof(serv_addr)); //Clearing serv_addr to ensure it is empty
    portNo = atoi((argv[1])); //Assigning the port number value

    serv_addr.sin_family = AF_INET; //Assigning values to our serv_addr structure
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portNo);

    if(bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){ //Creating/error checking bind
        error("Binding failed");
    }

    listen(sockfd, 5); //Who is trying to connect?
    clilen = sizeof(cli_addr);

    while(1){//After accepting connection, we get a new file descriptor
        newSockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);  //Accept connections
        if(newSockfd < 0){ //Accept error checking
            error("Error on accept");
        }
        workBuffer_insert(&workQueue, newSockfd);                           //Add connected socket to work queue
    }

    close(newSockfd); //Close client socket
    close(sockfd); //Close server socket
}

void *workerThread(){
    while(1){
        int socket = workBuffer_remove(&workQueue); //remove a socket from the queue & notify that there is an empty spot
        printf("New client connected\n");
        workHandler(socket);                        //service client
        close(socket);                              //close socket
    }
}

void *serverLogThread(){
    while(1){
        char* item = logBuffer_remove(&logQueue);
        sem_wait(&writeMutex); //Provide mutual exclusion for write to file
        fprintf(wp, "%s\n", item);
        sem_post(&writeMutex);
    }
}

void workHandler (int sock){
    char* buffer = malloc(sizeof(char) * 256);
    char input[256];
    int found = 0;
    char *sendMe;
    void *fd_pointer = &sock;
    int clientNum;
    while(1){
        for(size_t i = 0; i < NUMWORKERS; ++i){ //Getting the ids of the connected clients
            if(workerThreadId[i] == pthread_self()){
                clientNum = i;
                break;
            }
        }
        ////Reading from client///////
        int n = read(sock, buffer, 256); //Get Info from client
        sem_wait(&catMutex); //provide mutual exclusion while concatenating our string
        if(strncmp(buffer, "!exit", 5) == 0){ //If user enters "!exit" the program closes
            fclose(fp);
            fclose(wp);
            close(sock);
            exit(0);
        }
        if(n < 0){
            error("Error on reading");
        }

        while(fgets(input, 256, fp)){   //Checking if the inputted word is found in the file
            if(strcmp(input, buffer) == 0){
                found = 1;
                break;
            }
        }
        rewind(fp); //reset file pointer
        char *sendMe = concat(buffer, found, clientNum); //Concatenate string with OK or MISSPELLED
        printf("Message received from client: %d\n", clientNum);
        found = 0; //Reset

        logBuffer_insert(&logQueue, sendMe); //Insert concatenating string into log buffer
        bzero(buffer, 256); //Clearing buffer memory

        sem_post(&catMutex);//Release the concatenate lock

    }
}

char* concat(char* c, int found, int clientNum){
    char* returnMe;
    if(found == 1){
        returnMe = malloc(strlen(c) + ((sizeof(char) * 6)));
        returnMe = c;
        returnMe[strlen(c) - 1] = ' ';
        returnMe[strlen(c)] = 'O';
        returnMe[strlen(c)] = 'K';
        returnMe[strlen(c)] = ' ';
        returnMe[strlen(c)] = clientNum + '0';
        returnMe[strlen(c)] = '\n';
        returnMe[strlen(c)] = '\0';
    }
    else{
        returnMe = malloc(strlen(c) + ((sizeof(char) * 14)));
        returnMe = c;
        returnMe[strlen(c) - 1] = ' ';
        returnMe[strlen(c)] = 'M';
        returnMe[strlen(c)] = 'I';
        returnMe[strlen(c)] = 'S';
        returnMe[strlen(c)] = 'S';
        returnMe[strlen(c)] = 'P';
        returnMe[strlen(c)] = 'E';
        returnMe[strlen(c)] = 'L';
        returnMe[strlen(c)] = 'L';
        returnMe[strlen(c)] = 'E';
        returnMe[strlen(c)] = 'D';
        returnMe[strlen(c)] = ' ';
        returnMe[strlen(c)] = clientNum + '0';
        returnMe[strlen(c)] = '\n';
        returnMe[strlen(c)] = '\0';
    }

    return returnMe;
}
