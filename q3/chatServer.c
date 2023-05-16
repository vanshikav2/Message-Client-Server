/* server process */

/* include the necessary header files */
#include<ctype.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<stdlib.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<unistd.h>
#include<string.h>
// DO WE INCLUDE THIS LIBRARY???
#include<sys/select.h>

#include "protocol.h"
#include "libParseMessage.h"
#include "libMessageQueue.h"

/**
 * send a single message to client 
 * sockfd: the socket to read from
 * toClient: a buffer containing a null terminated string with length at most 
 * 	     MAX_MESSAGE_LEN-1 characters. We send the message with \n replacing \0
 * 	     for a mximmum message sent of length MAX_MESSAGE_LEN (including \n).
 * return 1, if we have successfully sent the message
 * return 2, if we could not write the message
 */
int sendMessage(int sfd, char *toClient){
    int len = strlen(toClient) + 1;
	toClient[strlen(toClient)] = '\n';
    int numSend = send(sfd, toClient, len, MSG_DONTWAIT);
    if(numSend < 0) {
        return 2;
    }
    return 1;	
}

/**
 * read a single message from the client. 
 * sockfd: the socket to read from
 * fromClient: a buffer of MAX_MESSAGE_LEN characters to place the resulting message
 *             the message is converted from newline to null terminated, 
 *             that is the trailing \n is replaced with \0
 * return 1, if we have received a newline terminated string
 * return 2, if the socket closed (read returned 0 characters)
 * return 3, if we have read more bytes than allowed for a message by the protocol
 */
int recvMessage(int sfd, char *fromClient){

    // loop through the message and see if a null character is reached
    // if it is then just return 1 with the whole message
    // if not then return 4 so that the whole loop runs again.
    // and after the more partial message is reciebed then append to the buffer with the 
    // previous message and return 1 so that message could be processed normally 

    char temp_buf[MAX_MESSAGE_LEN]; // temporary buffer 
    int num; // number of bytes 
    int len = 0; // counter
    int mess_check = 0; 
    num = recv(sfd, temp_buf, MAX_MESSAGE_LEN, 0);

    while (!mess_check) {
        
        if (num <= 0) {
            // error or connection closed
            return 2;
        }
        if (temp_buf[len] == '\n') {
            // found end of message
            fromClient[len] = '\0';
            mess_check = 1;
        } 
        // if end of message but its partial
        else if (len == num){
            // not working for twice 
            num += recv(sfd, temp_buf+len, MAX_MESSAGE_LEN-len, 0);
        }
        else {
            // append to message buffer
            fromClient[len] = temp_buf[len];
            len++;
          
            if (len == MAX_MESSAGE_LEN-1) {
                // message too long, truncate and mark as complete
                fromClient[len] = '\0';
                mess_check = 1;
                close(sfd);
            }
        }
    }
    return 1;
}

int max (int a, int b){
    if (a>b)return a;
    return b;
}

typedef struct CLIENT {
	int register_status;
	char *name;
    MessageQueue queue;
    char fromClient[MAX_MESSAGE_LEN];
    char toClient[MAX_MESSAGE_LEN];
} CLIENT;




int main (int argc, char ** argv) {

    int sockfd;

    if(argc!=2){
	    fprintf(stderr, "Usage: %s portNumber\n", argv[0]);
	    exit(1);
    }
    int port = atoi(argv[1]);

    if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
        perror ("socket call failed");
        exit (1);
    }

    struct sockaddr_in server;
    server.sin_family=AF_INET;          // IPv4 address
    server.sin_addr.s_addr=INADDR_ANY;  // Allow use of any interface 
    server.sin_port = htons(port);      // specify port

    if (bind (sockfd, (struct sockaddr *) &server, sizeof(server)) == -1) {
        perror ("bind call failed");
        exit (1);
    }

    if (listen (sockfd, 5) == -1) {
        perror ("listen call failed");
        exit (1);
    }

    CLIENT* a = malloc(sizeof(CLIENT) * MAX_USER_LEN);
    for (int i = 0; i < MAX_USER_LEN; i++){
        a[i].register_status = 0;
        a[i].name = NULL;
        initQueue(&a[i].queue);
        a[i].fromClient[0] = '\0';
        a[i].toClient[0]='\0';
    }

    /**
     * maintain a list of the connected fds. 
     * Select can manage at most 1024 fds. See poll for a newer api.
     */
    int fdlist[1024]; // clients list
    int fdcount=0; // amount of clients
    fdlist[0]=sockfd; // pay attention to the accept socket
    fdcount++;

    for (;;) {

			// START: Prepare for the select call 
	fd_set readfds, writefds, exceptfds;  // bit strings for the list of FDs we are interested in
        FD_ZERO(&readfds);                    // not interested in any fds yet for reading
        FD_ZERO(&writefds);                   //                                   writing
        FD_ZERO(&exceptfds);                  //                                   exceptions

	// setup readfds, identifying the FDs we are interested in waking up and reading from
        int fdmax=0;                          
        for (int i=0; i<fdcount; i++) {
            if (fdlist[i]>0) {
                FD_SET(fdlist[i], &readfds);  // poke the fdlist[i] bit of readfds
                FD_SET(fdlist[i], &writefds);
                fdmax=max(fdmax,fdlist[i]);
            }
        }

        struct timeval tv;  // how long we should sleep in case nothing happens
        tv.tv_sec=5;          
        tv.tv_usec=0;
	// END: Prepare for select call

	int numfds;
        if ((numfds=select(fdmax+1, &readfds, &writefds, &exceptfds, &tv))>0) { // block!!

            for (int i=0; i<fdcount; i++) {
            a[i].fromClient[0] = '\0';
            a[i].toClient[0]='\0';

                if (FD_ISSET(fdlist[i],&readfds)) {

                   if (fdlist[i]==sockfd) { /*accept a connection */
                        int newsockfd;

                        if ((newsockfd = accept (sockfd, NULL, NULL)) == -1) {
                            continue;
                        }
                        fdlist[fdcount++]=newsockfd;
                   
                    } else { /* read from an existing connection: guaranteed 1 non-blocking read */
                       
                    int retVal=recvMessage(fdlist[i], a[i].fromClient); 
                    
                    if(retVal==1){

                        // we have a null terminated string from the client
                        char *part[4];

                        int numParts=parseMessage(a[i].fromClient, part);


                        if(numParts==0){
                            strcpy(a[i].toClient,"ERROR");


                        } else if(strcmp(part[0], "list")==0){
                                int c;
                                char users[MAX_USER_LEN];
                                users[0] = '\0';
                                for (c = 1; c < fdcount; c++){
                                    if (a[c].register_status == 1){ // check if they are registered
                                        strcat(users, a[c].name); // add the name to the list
                                        strcat(users, " ");        

                                    }
                                    if (c > 11){
                                        break;
                                    }
                                }
                                sprintf(a[i].toClient, "users:%s", users); // use loop to print all the users



                        } else if(strcmp(part[0], "message")==0){
                                char *fromUser=part[1];
                                char *toUser=part[2];
                                char *message=part[3];

                                if(strcmp(fromUser, a[i].name)!=0 || a[i].register_status == 0){
                                    sprintf(a[i].toClient, "invalidFromUser:%s",fromUser);

                                    
                                } else {
                                    // check if toUser exists
                                    int checked = 0;
                                    for (int checker = 1; checker < fdcount; checker++){
                                        if(strcmp(toUser, a[checker].name)==0 && a[checker].register_status == 1){
                                            checked = 1;
                                            break;
                                        }
                            
                                    } 
                                    // invalid toUser
                                    if (checked == 0){
                                        sprintf(a[i].toClient, "invalidToUser:%s",toUser);

                                    }

                                    else{
                                    // once message complete, do the rest
                                    sprintf(a[i].toClient, "%s:%s:%s:%s","message", fromUser, toUser, message);
                                    int current;

                                    for (current = 1; current < fdcount; current++){
                                        if(strcmp(toUser, a[current].name)==0){
                                            break;
                                        }
                            
                                    } 

                                    if(enqueue(&a[current].queue, a[i].toClient)){
                                        strcpy(a[i].toClient, "messageQueued");

                                    }else{
                                        strcpy(a[i].toClient, "messageNotQueued");

                                    }
                                }
                                }
                    } else if(strcmp(part[0], "quit")==0){
                        strcpy(a[i].toClient, "closing");

                    } else if(strcmp(part[0], "getMessage")==0){
                        if(dequeue(&a[i].queue, a[i].toClient)){

                        } else {
                            strcpy(a[i].toClient, "noMessage");

                        }
                    } else if(strcmp(part[0], "register")==0){
                        if (a[i].name == NULL){    
                            a[i].name = malloc(sizeof(char) * MAX_USER_LEN);
                            strcpy(a[i].name, part[1]);
                            a[i].register_status = 1;
                            strcpy(a[i].toClient, "registered");

                        } else {
                            strcpy(a[i].toClient, "userAlreadyRegistered");

                        }
                    }
                    } 
                    else {
                            close (fdlist[i]);
                    }

		        } 
            }

            if (FD_ISSET(fdlist[i],&writefds)) {

                if(strcmp(a[i].toClient, "closing") == 0){
                    sendMessage(fdlist[i], a[i].toClient);
                    close(fdlist[i]);
                }
                
                // check that buffer is not empty
                if(strlen(a[i].toClient) != 0){
                    sendMessage(fdlist[i], a[i].toClient);
                }
                }
            }
        } 
    }
    free(a);
    exit(0);
}
