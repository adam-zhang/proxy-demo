#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <netdb.h>

#define TCP_PROTO　　 "tcp"
int proxy_port;　　　 /* port to listen for proxy connections on */
struct sockaddr_in hostaddr;　　 /* host addr assembled from gethostbyname() */
extern int errno;　　 /* defined by libc.a */
extern char *sys_myerrlist[];
void parse_args (int argc, char **argv);
void daemonize (int servfd);
void do_proxy (int usersockfd);
void reap_status (void);
void errorout (char *msg);
/*This is my modification.
I'll tell you why we must do this later*/
typedef void Sigfunc(int);     // Sigfunc可以用来申明一个参数为int的返回值为void的函数
/****************************************************************
function: 　　　main
description: 　 Main level driver. After daemonizing the process, a socket is opened to listen for connections on the proxy port, connections are accepted and children are spawned to handle each new connection.
arguments: 　　 argc,argv you know what those are.
return value: 　none.
calls: 　　　　 parse_args, do_proxy.
globals: 　　　 reads proxy_port.
****************************************************************/
main (argc,argv)
int argc;
char **argv;
{
　　　int clilen;
　　　int childpid;
　　　int sockfd, newsockfd;
　　　struct sockaddr_in servaddr, cliaddr;
　　　parse_args(argc,argv);
　　　/* prepare an address struct to listen for connections */
　　　bzero((char *) &servaddr, sizeof(servaddr));
　　　servaddr.sin_family = AF_INET;
　　　servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
　　　servaddr.sin_port = proxy_port;
　　　/* get a socket... */
　　　if ((sockfd = socket(AF_INET,SOCK_STREAM,0)) < 0) {
　　　　　fputs("failed to create server socket/r/n",stderr);
　　　　　exit(1);
　　　}
　　　/* ...and bind our address and port to it */
　　　if (bind(sockfd,(struct sockaddr_in *) &servaddr,sizeof(servaddr)) < 0) {
　　　　　fputs("faild to bind server socket to specified port/r/n",stderr);
　　　　　exit(1);
　　　　}
　　　/* get ready to accept with at most 5 clients waiting to connect */
　　　listen(sockfd,5);
　　/* turn ourselves into a daemon */
　　daemonize(sockfd);
　　/* fall into a loop to accept new connections and spawn children */
　　while (1) {
　　　　/* accept the next connection */
　　　　clilen = sizeof(cliaddr);
　　　　newsockfd = accept(sockfd, (struct sockaddr_in *) &cliaddr, &clilen);
　　　　if (newsockfd < 0 && errno == EINTR)
　　　　　　continue;
　　　　/* a signal might interrupt our accept() call */
　　　　else if (newsockfd < 0)
　　　　　　/* something quite amiss -- kill the server */
　　　　errorout("failed to accept connection");
　　　　/* fork a child to handle this connection */
　　　　if ((childpid = fork()) == 0) {
　　　　　　close(sockfd);
　　　　　　do_proxy(newsockfd);
　　　　　　exit(0);
　　　　　}
　　　　/* if fork() failed, the connection is silently dropped -- oops! */
　　　　　lose(newsockfd);
　　　　　}
　　　}

}

/****************************************************************
function:　　　 parse_args
description:　　parse the command line args.
arguments: 　　 argc,argv you know what these are.
return value: 　none.
calls: 　　　　 none.
globals: 　　　 writes proxy_port, writes hostaddr.
****************************************************************/
void parse_args (argc,argv)
int argc;
char **argv;
{
　　int i;
　　struct hostent *hostp;
　　struct servent *servp;
　　unsigned long inaddr;
　　struct {
　　　　char proxy_port [16];
　　　　char isolated_host [64];
　　　　char service_name [32];
　　} pargs;
　　if (argc < 4) {
　　　　　printf("usage: %s <proxy-port> <host> <service-name|port-number>/r/n", argv[0]);
　　　　　exit(1);
　　}
　　strcpy(pargs.proxy_port,argv[1]);
　　strcpy(pargs.isolated_host,argv[2]);
　　strcpy(pargs.service_name,argv[3]);
　　for (i = 0; i < strlen(pargs.proxy_port); i++)
　　　　if (!isdigit(*(pargs.proxy_port + i)))
　　　　　　break;
　　if (i == strlen(pargs.proxy_port))
　　　　proxy_port = htons(atoi(pargs.proxy_port));
　　else {
　　　　printf("%s: invalid proxy port/r/n",pargs.proxy_port);
　　　　exit(0);
　　}
　　bzero(&hostaddr,sizeof(hostaddr));
　　hostaddr.sin_family = AF_INET;
　　if ((inaddr = inet_addr(pargs.isolated_host)) != INADDR_NONE)
　　　　bcopy(&inaddr,&hostaddr.sin_addr,sizeof(inaddr));
　　else if ((hostp = gethostbyname(pargs.isolated_host)) != NULL)
　　　　bcopy(hostp->h_addr,&hostaddr.sin_addr,hostp->h_length);
　　else {
　　　　printf("%s: unknown host/r/n",pargs.isolated_host);
　　　　exit(1);
　　}
　　if ((servp = getservbyname(pargs.service_name,TCP_PROTO)) != NULL)
　　　　hostaddr.sin_port = servp->s_port;
　　else if (atoi(pargs.service_name) > 0)
　　　　hostaddr.sin_port = htons(atoi(pargs.service_name));
　　else {
　　　　printf("%s: invalid/unknown service name or port number/r/n", pargs.service_name);
　　　　exit(1);
　　}
}

/****************************************************************
function: 　　daemonize
description:　detach the server process from the current context, creating a pristine, predictable environment in which it will execute.
arguments:　　servfd file descriptor in use by server.
return value: none.
calls:　　　　none.
globals:　　　none.
****************************************************************/
void daemonize (servfd)
int servfd;
{
　　int childpid, fd, fdtablesize;
　　/* ignore terminal I/O, stop signals */
　　　signal(SIGTTOU,SIG_IGN);
　　　signal(SIGTTIN,SIG_IGN);
　　　signal(SIGTSTP,SIG_IGN);
　　/* fork to put us in the background (whether or not the user
　　 specified '&' on the command line */
　　if ((childpid = fork()) < 0) {
　　　　fputs("failed to fork first child/r/n",stderr);
　　　　exit(1);
　　　}
　　else if (childpid > 0)
　　　exit(0); /* terminate parent, continue in child */
　　　/* dissociate from process group */
　　if (setpgrp(0,getpid())<0) {
　　　　fputs("failed to become process group leader/r/n",stderr);
　　　　exit(1);
　　}
　　/* lose controlling terminal */
　　if ((fd = open("/dev/tty",O_RDWR)) >= 0) {
　　　　ioctl(fd,TIOCNOTTY,NULL);
　　　　close(fd);
　　}
　　/* close any open file descriptors */
　　for (fd = 0, fdtablesize = getdtablesize(); fd < fdtablesize; fd++)
　　if (fd != servfd)
　　　close(fd);
　　　/* set working directory to allow filesystems to be unmounted */
　　　chdir("/");
　　　/* clear the inherited umask */
　　　umask(0);
　　　/* setup zombie prevention */
　　　signal(SIGCLD,(Sigfunc *)reap_status);
　　}
}

/****************************************************************
function: 　　　reap_status
description: 　 handle a SIGCLD signal by reaping the exit status of the perished child, and discarding it.
arguments: 　　 none.
return value: 　none.
calls: 　　　　 none.
globals: 　　　 none.
****************************************************************/
void reap_status()
{
　　int pid;
　　union wait status;
　　while ((pid = wait3(&status,WNOHANG,NULL)) > 0)
　　; /* loop while there are more dead children */
}

/****************************************************************
function: 　　 do_proxy
description: 　does the actual work of virtually connecting a client to the telnet service on the isolated host.
arguments: 　　usersockfd socket to which the client is connected. return value: none.
calls: 　　　　none.
globals: 　　　　reads hostaddr.
****************************************************************/
void do_proxy (usersockfd)
int usersockfd;
{
　　int isosockfd;
　　fd_set rdfdset;
　　int connstat;
　　int iolen;
　　char buf[2048];
　　/* open a socket to connect to the isolated host */
　　if ((isosockfd = socket(AF_INET,SOCK_STREAM,0)) < 0)
　　errorout("failed to create socket to host");
　　/* attempt a connection */
　　connstat = connect(isosockfd,(struct sockaddr *) &hostaddr, sizeof(hostaddr));
　　switch (connstat) {
　　case 0:
　　break;
　　case ETIMEDOUT:
　　case ECONNREFUSED:
　　case ENETUNREACH:
　　strcpy(buf,sys_myerrlist[errno]);
　　strcat(buf,"/r/n");
　　write(usersockfd,buf,strlen(buf));
　　close(usersockfd);
　　exit(1);
　　/* die peacefully if we can't establish a connection */
　　break;
　　default:
　　errorout("failed to connect to host");
　　}
　　/* now we're connected, serve fall into the data echo loop */
　　while (1) {
　　　　/* Select for readability on either of our two sockets */
　　　　FD_ZERO(&rdfdset);
　　　　FD_SET(usersockfd,&rdfdset);
　　　　FD_SET(isosockfd,&rdfdset);
　　if (select(FD_SETSIZE,&rdfdset,NULL,NULL,NULL) < 0)
　　　errorout("select failed");
　　　/* is the client sending data? */
　　if (FD_ISSET(usersockfd,&rdfdset)) {
　　　　if ((iolen = read(usersockfd,buf,sizeof(buf))) <= 0)
　　　　　break; /* zero length means the client disconnected */
　　　　　rite(isosockfd,buf,iolen);
　　　　　/* copy to host -- blocking semantics */
　　　}
　　/* is the host sending data? */
　　if (FD_ISSET(isosockfd,&rdfdset)) {
　　　　f ((iolen = read(isosockfd,buf,sizeof(buf))) <= 0)
　　　　　　break; /* zero length means the host disconnected */
　　　　　　rite(usersockfd,buf,iolen);
　　　　　　/* copy to client -- blocking semantics */
　　　　}
　　　}
　　　/* we're done with the sockets */
　　　close(isosockfd);
　　　lose(usersockfd);
　　}
}

/****************************************************************
function:　　errorout
description: displays an error message on the console and kills the current process.
arguments: 　msg -- message to be displayed.
return value: none -- does not return.
calls: 　　　none.
globals:　　 none.
****************************************************************/
void errorout (msg)
char *msg;
{
　　FILE *console;
　　console = fopen("/dev/console","a");
　　fprintf(console,"proxyd: %s/r/n",msg);
　　fclose(console);
　　exit(1);
}
