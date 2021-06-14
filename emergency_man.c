#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>
#include <mqueue.h>


#define PIPE_NAME "input_pipe"
#define LOGFILE_SIZE 1 << 24
//#include <stats.h>

struct Paciente{
	int priority;
	int triage_time;
	int doctor_time;
	long arrival;
	char name[120];
}paciente;

typedef struct Statistics{
  int num_pacientes_triados;
  int num_pacientes_atendidos;
  int tempo_medio_espera_antes_triagem;
  int tempo_espera_triagem;
  int tempo_total;
}statistics;

FILE * log_file;
int log_fd;
char *log_ptr;
int idMQ;
struct Statistics stats;

pid_t *pid;
int shmid;
pthread_t *thread;
statistics *sharedmem;
int config_vars[4];
char mqueuebuff[100000]; 
pthread_mutex_t statistics_mut;
pthread_mutexattr_t statistics_attr;
int pipe_fd;
char pipe_input[100];

void createNewPaciente(){
	struct Paciente *p = malloc(sizeof(struct Paciente));
	p->priority = 0;
	p->triage_time = 0;
	p->doctor_time = 0;
	p->arrival = 0.0;
	strcpy(p->name,"");
	printf("New Paciente criado..\n");
}
void createNamedPipe(){
	//char input[256];
	//char str[50];
    //printf("Please write some text:\n");
    //scanf("%s", input);
	//printf("%s",input);
	if ((mkfifo(PIPE_NAME, O_CREAT|O_EXCL|0600)<0) && (errno!= EEXIST)){
		perror("Cannot create pipe: ");
	 	exit(0);
   	}
	if ((pipe_fd = open(PIPE_NAME, O_RDWR | O_NONBLOCK)) == -1){
		perror("Error opening pipe for reading");
	}
	printf("Pipeopened..\n");
	
	//write(pipe_fd, "%s", input);
	//printf("Escreveu");
    //if(fgets(str, 50, pipe_fd) != NULL){
     //   puts(str);
    //}
	close(pipe_fd);																																																																																																																																		
	printf("Created named pipe ...\n");
} 
/*
void readPipe(){
	if ((pipe_fd = open(PIPE_NAME, O_RDONLY)) == -1){
        perror("Error opening pipe for reading");
	}
	read(pipe_fd, pipe_input, sizeof(pipe_input));
	printf("Received %s from pipe\n", pipe_input);
	close(pipe_fd);
}
*/
int map_log_file(){
    log_file = fopen("log.txt","w");
    if((log_fd = open("log.txt", O_RDWR | O_CREAT, 0777)) == -1){
        perror("Failed to open log_file");
        fclose(log_file);
    }
    else printf("Log file Created...\n");
    if((log_ptr = mmap(0,LOGFILE_SIZE, PROT_WRITE, MAP_SHARED, log_fd, 0)) == MAP_FAILED){
        perror("Error maping log_file");    
    }
    else printf("Log file Maped...\n");
    return log_fd;
}

 
//creates message queue
void createMQ(){
    if ((idMQ=mq_open("/MQ2", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR, NULL)) == -1){
        perror("Error creating message queue");
        exit(1);
    }
    else
        printf("Created message queue.\n");
}

//returns percentage of ocupation of message queue, relative to MQ_MAX
float ocupation_MQ(){
    float ocupation;
    float percentage;
    struct mq_attr buff;

    mq_getattr(idMQ, &buff);

    ocupation = (float)(buff.mq_curmsgs);
    percentage = ocupation / config_vars[3];
    return percentage; 
}


//acrescenta na message queue
void send_mensage(char* mensagem, int priority){
    if(mq_send(idMQ,mensagem,strlen(mensagem),priority) == -1){
        perror("Error sending message.");
        exit(1);
    }

    printf("process %d:message %s sent\n",getpid(),mensagem);
    if(ocupation_MQ() >= 0.8){
        //CRIA PROCESSO EXTRA
        printf("MQ_ 80 reached!!!Temos de criar novo doutor\n");
    }


}

//le mensagem
char* read_message(){
    if(mq_receive(idMQ, mqueuebuff,100000, NULL) == -1){
        perror("Error reading message.");
        exit(1);
    }

    printf("process %d:message %s received\n",getpid(),mqueuebuff);
	
    if(ocupation_MQ() <= 0.8){
        //TERMINA PROCESSO EXTRA DEPIS DELE TERMINAR PACIENTE ATUAL
        printf("dropped below 0.8 of MQ_max!\n");
		
    }
    return mqueuebuff;
}

//write statistics to sm
void write_sm(int num_pacientes_triados, int num_pacientes_atendidos, int tempo_medio_espera_antes_triagem,
  int tempo_espera_triagem, int tempo_total){

    //TODO:FALTAM SEMAFOROS 

    sharedmem -> num_pacientes_triados = num_pacientes_triados;
    sharedmem -> num_pacientes_atendidos = num_pacientes_atendidos;
    sharedmem -> tempo_medio_espera_antes_triagem = tempo_medio_espera_antes_triagem;
    sharedmem -> tempo_espera_triagem = tempo_espera_triagem;
    sharedmem -> tempo_total = tempo_total;
}





//print statistic from sm()
void print_statistics(){
    printf("process %d:",getpid());
    printf("pacientes triados %d | ", sharedmem -> num_pacientes_triados);
    printf("pacientes atendidos %d | ", sharedmem -> num_pacientes_atendidos);
    printf("tme antes triagem %d | ", sharedmem -> tempo_medio_espera_antes_triagem);
    printf("te triagem %d | ", sharedmem -> tempo_espera_triagem);
    printf("te total %d | ", sharedmem -> tempo_total);
    printf("\n");
}



void lerConfig(){
	
	char url[]="config.txt";
	FILE *arq;


	arq = fopen(url, "r");
	if(arq == NULL)
		printf("Erro, nao foi possivel abrir o arquivo\n");
	else{
		if( (fscanf(arq,"TRIAGE=%d\nDOCTORS=%d\nSHIFT_LENGTH=%d\nMQ_MAX=%d\n",&config_vars[0],&config_vars[1],&config_vars[2],&config_vars[3]))==4){
			//printf("Read Sucessfull\n");
		}
	}
	//printf("%d %d %d %d ",config_vars[0],config_vars[1],config_vars[2],config_vars[3]);
	fclose(arq);
}

void create_mutex(){
	if(!pthread_mutexattr_init(&statistics_attr))perror("Erro a criar o mutex");
	if(!pthread_mutexattr_setpshared(&statistics_attr,PTHREAD_PROCESS_SHARED))perror("Erro a criar o mutex");
	if(!pthread_mutex_init(&statistics_mut,&statistics_attr))perror("Erro a criar mutex");
}

void destroy_mutex(){
	if(!pthread_mutex_destroy(&statistics_mut))perror("Erro a destruir o mutex mut");
	if(!pthread_mutexattr_destroy(&statistics_attr))perror("Erro a destruir o mutex attr");
	printf("Deleting mutex...\n");
}

void criarSharedMemory(){
    //adicionar semaforo mais tarde
    //create
	printf("Creating shared memory...\n");
	if((shmid = shmget(IPC_PRIVATE,sizeof(stats),0766|IPC_CREAT))==-1){
		perror("Erro na criacao da memoria\n");
		exit(1);
	}	
    //map
	if((sharedmem = shmat(shmid,NULL,0))==(statistics *)-1){
        perror("Erro no shmat");
		exit(1);
    }
	
}


void deleteSharedMemory(){
	signal(SIGINT,SIG_IGN);
	if(shmdt(sharedmem)==-1){
		perror("Erro ao apagar a memoria\n");
		exit(1);
	}
	if(shmctl(shmid,IPC_RMID,0)==-1){
		//perror("Erro no shmctl");
		exit(1);
	}
	printf("Shared memory deleting...\n");
	
}

void * triagem(void * id){
	pthread_mutex_lock(&statistics_mut);
	stats.num_pacientes_atendidos++;
	pthread_mutex_unlock(&statistics_mut);
	pthread_exit(NULL);
}

void createThreadPool(){
	int *id;
	printf("Creating threads\n");
	thread =(pthread_t *)malloc(config_vars[0]*sizeof(pthread_t));
	id = (int*)malloc(config_vars[0]*sizeof(int));
	for(int i=0;i<config_vars[0];i++){
		id[i]=i;
		if(pthread_create(&thread[i],NULL,triagem,(void*) id[i])==0)
			printf("Thread %d created\n",i);
		else perror("erro na criaçao das threads\n");
	}
	for(int i=0;i<config_vars[0];i++){
		pthread_join(thread[i],NULL);
		printf("Thread %d join\n",i);
	}
}

void doctorManager(int pid){
	time_t t = time(NULL);


    //TODO:APAGAR ISTO NO FIM
    write_sm(1,2,3,4,5);

    //read_message();
	

	while(time(NULL)-(long)t<config_vars[2]){
	   //listen on MQ here
	}
	pid = fork();
    if(pid==0){
    	printf("New doctor:%d\n",getpid());
      	doctorManager(pid);   	
      	exit(0);
    }
    else if(pid==-1){
      	perror("Erro");
		exit(1);
	}
}

//deletes message queue from system
void delete_mq(){
    mq_close(idMQ);
	printf("Deleting message queue...\n");
}

void fecharServer(){
	deleteSharedMemory();

    delete_mq();
	close(log_fd);
	for(int i=0;i<config_vars[1];i++){
		printf("Killing process %d \n",pid[i]);
    	kill(pid[i],SIGTERM);
	}
	free(pid);
	unlink(PIPE_NAME);
	remove(PIPE_NAME);
	//printf("wait\n");
	exit(0);
}


void doutor(){
  pid = (pid_t *)malloc(config_vars[1]*sizeof(pid_t));

  for(int i = 0;i<config_vars[1];i++){
    pid[i] = fork();
    if(pid[i]==0){
    	printf("pid do Filho: %d\n", getpid()); 

        //apagar aqui
        //sleep(2);
        ///////////


      	doctorManager(pid[i]);
        //wait(NULL);
        exit(0);
		
    }
    else if(pid[i]==-1){
        perror("fork");
        exit(1);
    }
  }
}


int main(void){
	int i;

    //only handle signals in main process
    //signal(SIGUSR1,print_statistics);
    signal(SIGINT,fecharServer);
	
    


    map_log_file();
    lerConfig();
	
	
	criarSharedMemory();
	createThreadPool();
    createMQ();
	createNamedPipe();
//	while(1){
		//readPipe();
	//}

	doutor();

	createNewPaciente();


    //apagar aqui
    for(i = 0; i < 4; i++)
        send_mensage("priori1", 1);

    for(i = 0; i < 4; i++)
        send_mensage("priori2", 2);

    //sleep(2);
    print_statistics();
    


    //wait for all child processes to finish
   	for(;;){wait(NULL);}
	
	//printf("%d\n",stats.num_pacientes_atendidos);
	return 0;
}
