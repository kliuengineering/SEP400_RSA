// client2.cpp - A client that communicates with a second client using triple RSA encrpytion/decryption
#include <arpa/inet.h>
#include <iostream>
#include <math.h>
#include <net/if.h>
#include <netinet/in.h>
#include <queue>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

using namespace std;

const char IP_ADDR[]="127.0.0.1";
bool is_running;
int srcPort=1155;
int destPort=1153;

// Encrpytion/Decryption variables
double n;
double e;
double d;
double phi;

queue<unsigned char*> messageQueue;
pthread_mutex_t lock_x;


// message variables
const int numMessages = 5;
const int BUF_LEN=256;
const unsigned char messages[numMessages][BUF_LEN]=
{
        "You were lucky to have a room. We used to have to live in a corridor.",
        "Oh we used to dream of livin' in a corridor! Woulda' been a palace to us.",
        "We used to live in an old water tank on a rubbish tip.",
        "We got woken up every morning by having a load of rotting fish dumped all over us.",
        "Quit"
};





// global variables
int sockfd;
struct sockaddr_in addr_src;
struct sockaddr_in addr_dest;
pthread_t pthread_id_recv;
pthread_t pthread_id_send;


// utility -> shutdown
static void shutdownHandler(int sig)
{
    switch(sig) 
    {
        case SIGINT:
            is_running=false;
            break;
    }
}


// utility -> Returns a^b mod c
unsigned char PowerMod(int a, int b, int c)
{
    int res = 1;
    for(int i=0; i<b; ++i) 
    {
        res=fmod(res*a, c);
    }
    return (unsigned char)res;
}
  

// utility -> Returns gcd of a and b
int gcd(int a, int h)
{
    int temp;
    while (1)
    {
        temp = a%h;
        if (temp == 0)
          return h;
        a = h;
        h = temp;
    }
}


// utility -> encryption
void Encryption(const unsigned char (&msg)[BUF_LEN], unsigned int (&msg_encoded)[BUF_LEN])
{
    /*
        Given: n, e, d, phi
    */

    unsigned int message = 0;
    unsigned int message_transformed = 0;
    unsigned int index = 0;

    // c = m^e * ( mod(n) )
    // e = (p-1) * (q-1)

    do
    {
        message = int (msg[index]);
        message_transformed = PowerMod(message, e, n);

        msg_encoded[index] = message_transformed;

        index++;
    } 
    // encrypt until null terminator
    while (message != 0);
}


// utility -> decryption
void Decryption(unsigned int *msg_encoded)
{
    unsigned char *message_decoded = new unsigned char[BUF_LEN];

    int message = 0;
    int index = 0;

    do
    {
        message = int (*msg_encoded);
        message_decoded[index] = (unsigned char) PowerMod(message, d, n);
        
        msg_encoded++;
        index++;
    } 
    // decrypt until null terminator
    while (message != 0);
    
    pthread_mutex_lock(&lock_x);
    messageQueue.push(message_decoded);
    pthread_mutex_unlock(&lock_x);
}


// utility -> sets up UDP socket
int SetupSocketUDP()
{
    // also sets up as non blocking
    sockfd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (sockfd < 0)
    {
        std::cerr << "socket creation failed...\n";
        exit(EXIT_FAILURE);
    }

    memset(&addr_src, 0, sizeof(addr_src));

    // info setup
    addr_src.sin_family = AF_INET;
    addr_src.sin_port = htons(srcPort);

    if (inet_pton(AF_INET, IP_ADDR, &addr_src.sin_addr) < 0)
    {
        std::cerr << "source port setup failure...\n";
        exit(EXIT_FAILURE);
    }

    // bind()
    if ( bind(sockfd, (const struct sockaddr *)&addr_src, sizeof(addr_src) ) < 0 )
    {
        std::cerr << "socket creation failed...\n";
        exit(EXIT_FAILURE);        
    }

    return sockfd;
}


// utility -> sets up destination port
void SetupDestination()
{
    memset(&addr_dest, 0, sizeof(addr_dest));
    addr_dest.sin_family = AF_INET;
    addr_dest.sin_port = htons(destPort);

    if ( inet_pton(AF_INET, IP_ADDR, &addr_dest.sin_addr) < 0 )
    {
        std::cerr << "destination port setup failure...\n";
        exit(EXIT_FAILURE);
    }
}


// utility -> sends out message
void SendMessageUDP(int sockfd, const sockaddr_in &addr, const char *message, int message_length)
{
    sendto (sockfd, message, message_length, MSG_CONFIRM, (const struct sockaddr *)&addr, sizeof(addr) );
    std::cout << "message is sent...\n";
}


// utility -> send thread
void *send_func(void *arg)
{
    int fd_source = *(int *)arg;

    unsigned int message_encoded[numMessages][BUF_LEN];

    // encryption
    for (int i=0; i<numMessages; i++)
    {
        memset(&message_encoded[i], 0, sizeof(message_encoded));
        Encryption(::messages[i], message_encoded[i]);
    }

    // let's send the messages
    for (int i=0; i<numMessages; i++)
    {
        sendto
        (
            sockfd, 
            (const unsigned int *)message_encoded[i], 
            BUF_LEN * sizeof(unsigned int),
            0,
            (struct sockaddr *)&addr_dest,
            sizeof(addr_dest)
        );

        sleep(1);
    }

    pthread_exit(NULL);
}


// utility -> receive thread
void *recv_func(void *arg)
{
    int fd_source = *(int *)arg;
    int len = 0;
    unsigned int message_encoded[BUF_LEN];
    unsigned char message_decoded[BUF_LEN];
    memset(&message_encoded, 0, BUF_LEN);
    memset(&message_decoded, 0, BUF_LEN);

    while (is_running)
    {
        len = recvfrom
        (
            sockfd, 
            message_encoded, 
            BUF_LEN * sizeof(unsigned int),
            0,
            NULL,
            NULL
        );

        if (len < 0)
        {
            sleep(1);
        }
        
        else
        {
            Decryption(message_encoded);
            memset(&message_encoded, 0, BUF_LEN);
        }
    }

    pthread_exit(NULL);
}


// utility -> message reader
void MessageReader(void)
{
    unsigned char *iterator = nullptr;

    while (is_running)
    {
        pthread_mutex_lock(&lock_x);

        if (!messageQueue.empty())
        {
            iterator = messageQueue.front();
            messageQueue.front() = nullptr;
            messageQueue.pop();
        }

        pthread_mutex_unlock(&lock_x);

        if (iterator)
        {
            if ( strncmp((char *)iterator, "Quit", 4) == 0 )
            {
                is_running = false;
            }

            else
            {
                std::cout << "Message Received -> " << iterator << std::endl;
            }

            delete []iterator;
            iterator = nullptr;
        }

        sleep(1);
    }
}


// Code to demonstrate RSA algorithm
int main()
{
    // Two random prime numbers
    double p = 11;
    double q = 23;
  
    // First part of public key:
    n = p*q;
  
    // Finding other part of public key.
    // e stands for encrypt
    e = 2;
    phi = (p-1)*(q-1);
    while (e < phi)
    {
        // e must be co-prime to phi and
        // smaller than phi.
        if (gcd((int)e, (int)phi)==1)
            break;
        else
            e++;
  
    }

    // Private key (d stands for decrypt)
    // choosing d such that it satisfies
    // d*e = 1 + k * totient
    int k = 2;  // A constant value
    d = (1 + (k*phi))/e;
    cout<<"p:"<<p<<" q:"<<q<<" n:"<<n<<" phi:"<<phi<<" e:"<<e<<" d:"<<d<<endl;





    //TODO: Complete the rest

    // setup shutdown ISR
    signal(SIGINT, shutdownHandler);

    // initializes the mutex
    pthread_mutex_init(&lock_x, NULL);

    // setup UDP socket
    int sockfd = SetupSocketUDP();

    // setup destination port
    SetupDestination();

    // sets the infinite loop
    is_running = true;

    // starts the receive thread
    if ( pthread_create(&pthread_id_recv, NULL, recv_func, &sockfd) < 0)
    {
        std::cerr << "failed to start the receive thread...\n";
        exit(EXIT_FAILURE);
    }

    // delays for 5s per the requirement
    sleep(5);

    // starts the send thread
    if ( pthread_create(&pthread_id_send, NULL, send_func, &sockfd) < 0 )
    {
        std::cerr << "failed to start the send thread...\n";
        exit(EXIT_FAILURE);
    }

    // message reading
    MessageReader();





    // cleans up
    pthread_join(pthread_id_recv, NULL);
    pthread_join(pthread_id_send, NULL);
    close(sockfd);
}