#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <netdb.h>
#include <time.h>
   

/* Threads de transmissao e recepcao */
void *transmissor(void *arg);
void *receiver(void *arg);
struct pthread_args
{
    int sock;
    char filename[100];
    //char *filename;
};

/* Thread de retransmissao */
void *resender(void *arg);
struct resend_args
{
    int sock;
    uint8_t *flag;
    unsigned char *frame;
};

/* Funcoes de codificacao e checksum */
unsigned char *encode16(uint8_t*, uint32_t);
uint8_t *decode16(unsigned char*, uint32_t);
uint16_t checksum(void *data, uint32_t bytes);

int
main(int argc, char *argv[])
{
    /* Verifica o numero de argumentos */
    if(argc != 5)
    {
        printf("Erro! Numero de argumentos invalido.\n");
        return 1;
    }

    /* Cria o socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1)
    {
        printf("Erro! Criacao do socket.\n");
        return 1;
    }
    struct sockaddr_in serv;
    socklen_t svsize = sizeof(struct sockaddr_in);

     /* Verifica o tipo de ponta a ser executada */
    if(!strcmp(argv[1], "-s"))  //Ponta passiva (servidor)
    {
        printf("Ponta passiva (servidor)\n");

        serv.sin_port = htons(atoi(argv[2]));
        serv.sin_addr.s_addr = htonl(INADDR_ANY);

        /* Realiza bind na porta e endereco */
        if(bind(sock, (struct sockaddr *) &serv, svsize) == -1)
        {
            printf("Erro! em bind.\n");
            return 1;
        }

        /* Escuta conexoes */
        if(listen(sock, 5) == -1)
        {
            printf("Erro! em listen.\n");
            return 1;
        }
    }
    else if(!strcmp(argv[1], "-c"))  //Ponta ativa (cliente)
    {
        printf("Ponta ativa (cliente)\n");

        /* Recuperar endereço e porta */
        char *domain = argv[2];
        int i = 0;
        char ip[15] = {0};
        int8_t dotcounter = 3;
        while(dotcounter >= 0)
        {
            char aux = *(domain)++;
            if((aux == '.') || (aux == ':'))    dotcounter--;
            if(aux == ':')
                ip[i++] = '\0';
            else
                ip[i++] = aux;
        }

        serv.sin_port = htons(atoi(domain)); //Insere a porta
        inet_aton(ip, &serv.sin_addr);
        
        /* Conecta a ponta passiva */
        if(connect(sock, (struct sockaddr *) &serv, svsize) == -1)
        {
            printf("Erro! em connect.\n");
            return 1;
        }
printf("Cliente conectado.\n");
    }

    int clisock = -1;
    while(1)    {   //Executa ate
        /* Espera por conexoes se passivo */
        if(!strcmp(argv[1], "-s"))
        {
            /* Aceita conexoes */
printf("Esperando conexoes.\n");
            clisock = accept(sock, (struct sockaddr *) &serv, &svsize);
            if(clisock < 0)
            {
                printf("Erro! em accept.\n");
                return 1;
            }
printf("Conectou-se, %d.\n", clisock);
        }

        /* Cria as threads de transmissao e recebimento */
        pthread_t receive, transmite;
        struct pthread_args recvarg, sendarg; //Argumentos das threads
        if(!strcmp("-c", argv[1]))    sendarg.sock = sock;
        else if(!strcmp("-s", argv[1]))  sendarg.sock = clisock;
        strcpy(sendarg.filename, argv[3]);
        if(pthread_create(&transmite, NULL, &transmissor, (void *) &sendarg) != 0)  //Thread de transmissao
        {
            printf("Erro! ao criar thread de transmissao.\n");
            return 1;
        }
        recvarg.sock = sock;
        if(!strcmp("-c", argv[1]))    recvarg.sock = sock;
        else if(!strcmp("-s", argv[1]))  recvarg.sock = clisock;
        strcpy(recvarg.filename, argv[4]);
        if(pthread_create(&receive, NULL, &receiver, (void *) &recvarg) != 0)  //Thread de recepcao
        {
            printf("Erro! ao criar thread de recepcao.\n");
            return 1;
        }

        /* Espera o fim de execucao das threads */
        int retval; //Retorno da thread
        pthread_join(receive, (void **) &retval);
        pthread_join(transmite, (void **) &retval);
        if(retval == 1) //Verifica se o no remoto fechou-se
        {
            printf("O no remoto foi fechado.\n");
            return 1;
        }
    }

    close(sock);

    return 0;
}

void*
transmissor(void *arg)
{
    /* Recupera os argumentos passados */
    struct pthread_args args = *((struct pthread_args *) arg);
    
    /* Abre o arquivo para transmissao */
    FILE *filefd = fopen(args.filename, "rb");
    if(filefd == NULL)
    {
        printf("Erro! ao abrir o arquivo para transmissao.\n");
        pthread_exit(NULL);
        return NULL;
    }

    /* Inicia linha de leitura e transmissao do arquivo */
    uint8_t read[0xffff] = {0x00}; //Vetor de dados do arquivo
    uint8_t id = 0x01;
    uint8_t lastID = 0x01;
    uint8_t nextID = 0x00;
    do
    {
        /* Le do arquivo */
        uint16_t size = 0;
        for(uint16_t i = 0; i < 0xffff; i++)
            size = (uint16_t) fscanf(filefd, "%hhx", &read[i]);

        /* Cria quadro */
        //uint8_t frame[0xffff + 0x001c];
        uint8_t *frame = (uint8_t *) malloc((size + 14) * sizeof(uint8_t));
        memset(frame, 0, (size + 28));
        /* Campos de sincronizacao */
        frame[0] = 0xdc;
        frame[1] = 0xc0;
        frame[2] = 0x23;
        frame[3] = 0xc2;
        frame[4] = 0xdc;
        frame[5] = 0xc0;
        frame[6] = 0x23;
        frame[7] = 0xc2;
        /* Campo de tamanho */
        uint16_t tam = htons(size); //Converte para network-byte-order
        frame[8] = ((uint8_t) (tam >> 8));
        frame[9] = ((uint8_t) tam);
        /* Campo de checksum */
        frame[10] = 0x00;
        frame[11] = 0x00;
        /* Campo de ID */
        id = (id == 0x00);
        frame[12] = id;
        /* Campo de flag */
        frame[13] = 0x00;
        /* Dados */
        for(uint16_t i = 0; i < size; i++)
            frame[i + 14] = read[i];

        /* Calcula o checksum */
        uint16_t chksum = checksum(frame, ((uint32_t) (size + (112 / 8))));
        frame[8] = ((uint8_t) (chksum >> 8));
        frame[9] = ((uint8_t) chksum);

        /* Codifica o quadro */
        unsigned char *sendFrame = encode16(frame, ((uint32_t) (size + (112 / 8))));

        /* Envia o quadro para o no remoto */
        /*if(send(args.sock, (unsigned char *) sendFrame, strlen(sendFrame), 0) == -1)
        {
            printf("Erro! ao enviar quadro id = %x.\n", id);
            pthread_exit((void *) 2);
            return NULL;
        }
        */

        /* Cria thread de envio e reenvio */
        pthread_t resend;
        uint8_t flag = 0;
        struct resend_args reargs;
        reargs.frame = sendFrame;
        reargs.flag = &flag;
        if(pthread_create(&resend, NULL, &resender, (void *) &reargs) != 0)
        {
            printf("Erro! ao criar thread de reenvio.\n");
            pthread_exit((void *) 3);
            return NULL;
        }

        /* Recebe confirmacao */
        //ssize_t recsize = recv(args.sock, (unsigned char *) recvFrame, (0xffff + 0x001c), 0);
        unsigned char recvFrame[0xffff + 0x000e];
        memset(recvFrame, 0x00, (0xffff + 0x000e));
        ssize_t recsize = recv(args.sock, (unsigned char *) recvFrame, 0x000e, 0);
        if(recsize == 0)    //O no remoto se desconectou
        {
            printf("Erro! No remoto se desconectou.\n");
            pthread_exit((void *) 1);
            return NULL;
        }
        else if(recsize == -1)
        {
            printf("Erro! no recebimento da confirmacao.\n");
            pthread_exit((void *) 2);
            return NULL;
        }

        /* Verifica a validade do quadro de confirmacao */
        uint8_t *convFrame = decode16(recvFrame, (uint32_t) recsize); //Decodifica o quadro recebido
        /* Verifica sincronizacao */
        uint8_t valido = 1;
        for(uint8_t i = 0; i < 2; i++)
        {
            if((convFrame[0] != 0xdc) || (convFrame[1] != 0xc0) || (convFrame[2] != 0x23) || (convFrame[3] != 0xc2))
            {
                printf("Erro! de sincronizacao.\n");
                valido = 0;
                i = 2;
            }
        }
        /* Verifica o id do quadro */
        if(valido && ((lastID == convFrame[12]) || (nextID != convFrame[12])))
            valido = 0;

        /* Verifica o checksum do quadro */
        uint16_t recvChksum = (((uint16_t) convFrame[10]) << 8) + ((uint16_t) convFrame[11]);
        convFrame[10] = 0x00;
        convFrame[11] = 0x00;
        if(valido && (recvChksum != checksum(convFrame, 0x000e)))
            valido = 0;

        /* Muda variaveis do stop-n-wait */
        if(valido)
        {
            printf("Recebido confirmacao de %u.\n", convFrame[12]);
            lastID = convFrame[12];
            nextID = (nextID == 0x00);
            flag = 1;
            pthread_join(resend, NULL); //Termina a thread de reenvio
        }
    }
    while(!feof(filefd));

    fclose(filefd); //Fecha o arquivo
    pthread_exit(NULL);
    return NULL;
}

void*
receiver(void *arg)
{
    /* Recupera os argumentos passados */
    struct pthread_args args = *((struct pthread_args *) arg);
    
    /* Abre o arquivo para escrita */
    FILE *filefd = fopen(args.filename, "wb");
    if(filefd == NULL)
    {
        printf("Erro! ao abrir o arquivo para recepcao.\n");
        pthread_exit(NULL);
        return NULL;
    }
    
    /* Inicia recepcao do arquivo */
    uint8_t lastID = 0x01;
    uint8_t nextID = 0x00;
    ssize_t recsize = 0;
    uint16_t lastChksum = 0x0000;
    do
    {
        /* Recebe quadro */
        unsigned char recvFrame[0x000e + 0xffff];
        memset(recvFrame, 0x00, (0xffff + 0x000e));
        recsize = recv(args.sock, (unsigned char *) recvFrame, (0xffff + 0x000e), 0);
printf("Recebido %s.\n", recvFrame);
        if(recsize == 0)    //O no remoto se desconectou
        {
            printf("Erro! No remoto se desconectou.\n");
            pthread_exit((void *) 1);
            return NULL;
        }
        else if(recsize == -1)
        {
            printf("Erro! no recebimento do quadro.\n");
perror("recv-frame receiver");
            pthread_exit(NULL);
            return NULL;
        }
        
        /* Verifica a validade do quadro */
        uint8_t *convFrame = decode16(recvFrame, (uint32_t) recsize); //Decodifica o quadro recebido
        /* Verifica sincronizacao */
        uint8_t valido = 1;
        for(uint8_t i = 0; i < 2; i++)
        {
            if((convFrame[0] != 0xdc) || (convFrame[1] != 0xc0) || (convFrame[2] != 0x23) || (convFrame[3] != 0xc2))
            {
                printf("Erro! de sincronizacao.\n");
                valido = 0;
                i = 2;
            }
        }
        
        /* Verifica o checksum do quadro */
        uint16_t recvChksum = (((uint16_t) convFrame[10]) << 8) + ((uint16_t) convFrame[11]);
        convFrame[10] = 0x00;
        convFrame[11] = 0x00;
        if(valido && (recvChksum != checksum(convFrame, 0x000e)))
            valido = 0;
            
        /* Verifica o id do quadro */
        //if(valido && ((lastID == convFrame[12]) || (nextID != convFrame[12])))
        if(valido)
        {
            /* Reenvia confirmacao de quadro ja recebido */
            if((lastID == convFrame[12]) && (lastChksum == recvChksum))
                valido = 2;
            else if(nextID != convFrame[12])
                valido = 0;
        }

        /* Muda variaveis do stop-n-wait */
        if(valido == 1)
        {
            printf("Recebido quadro %u.\n", convFrame[12]);
            lastID = convFrame[12];
            nextID = (nextID == 0x00);
            
            /* Escreve no arquivo */
            uint16_t tam = ((uint16_t) (convFrame[8] << 8)) + ((uint16_t) convFrame[9]);
            tam = ntohs(tam);
            for(uint16_t i = 0; i < tam; i++)
                fprintf(filefd, "%x", convFrame[i + 14]);
        }
        
        if(valido > 0)
        {
            /* Envia confirmacao de quadro ja recebido */
            /* Cria quadro */
            uint8_t frame[0x000e];
            memset(frame, 0, 0x000e);
            /* Campos de sincronizacao */
            frame[0] = 0xdc;
            frame[1] = 0xc0;
            frame[2] = 0x23;
            frame[3] = 0xc2;
            frame[4] = 0xdc;
            frame[5] = 0xc0;
            frame[6] = 0x23;
            frame[7] = 0xc2;
            /* Campo de tamanho */
            uint16_t tam = htons(0x0000); //Converte para network-byte-order
            frame[8] = ((uint8_t) (tam >> 8));
            frame[9] = ((uint8_t) tam);
            /* Campo de checksum */
            frame[10] = 0x00;
            frame[11] = 0x00;
            /* Campo de ID */
            //id = (id == 0x00);
            frame[12] = lastID;
            /* Campo de flag */
            frame[13] = 0x80;

            /* Calcula o checksum */
            uint16_t chksum = checksum(frame, ((uint32_t) (112 / 8)));
            frame[8] = ((uint8_t) (chksum >> 8));
            frame[9] = ((uint8_t) chksum);

            /* Codifica o quadro */
            unsigned char *sendFrame = encode16(frame, (((uint32_t) (112 / 8))));
            if(send(args.sock, (unsigned char *) sendFrame, 0x000e, 0) == -1)
            {
                printf("Erro! ao reenviar confirmacao.\n");
                return NULL;
            }
        }
    }
    while(recsize == (0xffff + 0x000e)); //Recebe enquanto nao e o quadro final
    
    fclose(filefd); //Fecha o arquivo
    pthread_exit(NULL);
    return NULL;
}

void*
resender(void *arg)
{
    /* Recupera os argumentos passados */
    struct resend_args args = *((struct resend_args *) arg);

    /* Verifica validade do reenvio */
    while(*(args.flag) == 0)
    {
printf("Enviando %s.\n", args.frame);
        /* Envia o quadro para o no remoto */
        if(send(args.sock, (unsigned char *) args.frame, strlen((char *) args.frame), 0) == -1)
        {
            printf("Erro! ao reenviar quadro.\n");
            pthread_exit(NULL);
            return NULL;
        }
        sleep(1);
    }

    return NULL;
}

unsigned char *encode16(uint8_t *buffer, uint32_t nsize)
{
    unsigned char *coded = (unsigned char *) malloc(nsize * sizeof(unsigned char));

    for(uint32_t i = 0; i < nsize; i++)
        sprintf((char *) &coded[i], "%x", buffer[i]);

    return coded;
}

uint8_t *decode16(unsigned char *buffer, uint32_t csize)
{
    uint8_t *decoded = (uint8_t *) malloc(csize * sizeof(uint8_t));

    for(uint32_t i = 0; i < csize; i++)    {
        unsigned char data = buffer[i];

        if((data >= 'a') && (data <= 'f'))
            decoded[i] = data - 97 + 10;
        else if((data >= 'A') && (data <= 'F'))
            decoded[i] = data - 65 + 10;
        else if((data >= '0') && (data <= '9'))
            decoded[i] = data - 48;
    }

    return decoded;
}

uint16_t checksum(void *data, uint32_t bytes)
{
    uint32_t sum = 0;

    while(bytes > 1)    {
        sum += (*(uint16_t *) data)++;
        bytes -= 2;
    }

    if(bytes > 0)
        sum += *((uint8_t *) data);

    while(sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    return (~sum);
}


