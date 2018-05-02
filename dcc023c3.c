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
};

/* Thread de retransmissao */
void *resender(void *arg);
struct resend_args
{
    int sock;
    uint8_t *flag;
    char *frame;
};

/* Funcoes de codificacao e checksum */
char *encode16(uint8_t*, uint32_t);
uint8_t *decode16(char*, uint32_t);
uint16_t checksum(void *data, uint32_t bytes);

int out = 0;

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
    serv.sin_family = AF_INET;

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
    }

    int clisock = -1;
    while(1)    {   //Executa ate
        /* Espera por conexoes se passivo */
        if(!strcmp(argv[1], "-s"))
        {
            /* Aceita conexoes */
            clisock = accept(sock, (struct sockaddr *) &serv, &svsize);
            if(clisock < 0)
            {
                printf("Erro! em accept.\n");
                return 1;
            }
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
        pthread_join(receive, NULL);
        pthread_join(transmite, NULL);
        if(out == 1) //Verifica se o no remoto fechou-se
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
        for(uint16_t i = 0; (i < 0xffff) && (!feof(filefd)); i++)
            //size = (uint16_t) fscanf(filefd, "%hhx", &read[i]);
            size = (uint16_t) fread(read, sizeof(uint8_t), 0xffff, filefd);
printf("Lido %hu bytes.\n", size);

        /* Cria quadro */
        uint8_t frameInit[0xffff + 0x001c];
        //uint8_t *frameInit = (uint8_t *) malloc((size + 14) * sizeof(uint8_t));
        memset(frameInit, 0, (size + 28));
        /* Campos de sincronizacao */
        frameInit[4] = 0xdc;
        frameInit[5] = 0xc0;
        frameInit[6] = 0x23;
        frameInit[7] = 0xc2;
        frameInit[0] = 0xdc;
        frameInit[1] = 0xc0;
        frameInit[2] = 0x23;
        frameInit[3] = 0xc2;
        /* Campo de tamanho */
        uint16_t tam = htons(size); //Converte para network-byte-order
        frameInit[8] = (tam >> 8);
        frameInit[9] = (tam);
        /* Campo de checksum */
        frameInit[10] = 0x00;
        frameInit[11] = 0x00;
        /* Campo de ID */
        id = (id == 0x00);
        frameInit[12] = id;
        /* Campo de flag */
        frameInit[13] = 0x00;
        /* Dados */
        for(uint32_t i = 0; i < ((uint32_t) size); i++)
            frameInit[i + 14] = read[i];

printf("Antes: [0x%x%x%x%x].\n", frameInit[0], frameInit[1], frameInit[2], frameInit[3]);
printf("length = %x %x.\n", frameInit[8], frameInit[9]);
        /* Calcula o checksum */
        uint16_t chksum = checksum(frameInit, ((uint32_t) (size + (112 / 8))));
        frameInit[10] = ((uint8_t) (chksum >> 8));
        frameInit[11] = ((uint8_t) chksum);
printf("Antes: [0x%x%x%x%x].\n", frameInit[0], frameInit[1], frameInit[2], frameInit[3]);
printf("length = %x %x.\n", frameInit[8], frameInit[9]);

        /* Codifica o quadro */
        char *sendFrame = encode16(frameInit, ((uint32_t) (size + (112 / 8))));
//printf("Enviando [%s].\n", sendFrame);
printf("Lido %hu bytes.\n", size);
        //free(frameInit);

        /* Envia o quadro para o no remoto */
        /*
        if(send(args.sock, (char *) sendFrame, strlen((char *) sendFrame), 0) == -1)
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
        reargs.sock = args.sock;
        reargs.frame = sendFrame;
        reargs.flag = &flag;
        if(pthread_create(&resend, NULL, &resender, (void *) &reargs) != 0)
        {
            printf("Erro! ao criar thread de reenvio.\n");
            pthread_exit(NULL);
            return NULL;
        }

        /* Recebe confirmacao */
        //ssize_t recsize = recv(args.sock, (char *) recvFrame, (0xffff + 0x001c), 0);
        char recvFrame[0xffff + 0x000e];
        memset(recvFrame, 0x00, (0xffff + 0x000e));
        ssize_t recsize = recv(args.sock, (char *) recvFrame, 0x000e, 0);
        if(recsize == 0)    //O no remoto se desconectou
        {
            printf("Erro! No remoto se desconectou.\n");
            out = 1;
            pthread_exit(NULL);
            return NULL;
        }
        else if(recsize == -1)
        {
            printf("Erro! no recebimento da confirmacao.\n");
            pthread_exit(NULL);
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
            free(sendFrame);
        }
        free(convFrame);
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
        char recvFrame[0x000e + 0xffff];
        memset(recvFrame, 0x00, (0xffff + 0x000e));
        recsize = recv(args.sock, (char *) recvFrame, (0xffff + 0x000e), 0);
printf("Recebido [%s].\n", recvFrame);
        if(recsize == 0)    //O no remoto se desconectou
        {
            printf("Erro! No remoto se desconectou.\n");
            out = 1;
            pthread_exit(NULL);
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
                //fprintf(filefd, "%x", convFrame[i + 14]);
                fwrite(&convFrame[i + 14], sizeof(uint8_t), tam, filefd);
        }
        
        if(valido > 0)
        {
            /* Envia confirmacao de quadro ja recebido */
            /* Cria quadro */
            uint8_t frameAck[0x000e];
            memset(frameAck, 0, 0x000e);
            /* Campos de sincronizacao */
            frameAck[0] = 0xdc;
            frameAck[1] = 0xc0;
            frameAck[2] = 0x23;
            frameAck[3] = 0xc2;
            frameAck[4] = 0xdc;
            frameAck[5] = 0xc0;
            frameAck[6] = 0x23;
            frameAck[7] = 0xc2;
            /* Campo de tamanho */
            uint16_t tam = htons(0x0000); //Converte para network-byte-order
            frameAck[8] = ((uint8_t) (tam >> 8));
            frameAck[9] = ((uint8_t) tam);
            /* Campo de checksum */
            frameAck[10] = 0x00;
            frameAck[11] = 0x00;
            /* Campo de ID */
            //id = (id == 0x00);
            frameAck[12] = lastID;
            /* Campo de flag */
            frameAck[13] = 0x80;

            /* Calcula o checksum */
            uint16_t chksum = checksum(frameAck, ((uint32_t) (112 / 8)));
            frameAck[8] = ((uint8_t) (chksum >> 8));
            frameAck[9] = ((uint8_t) chksum);

            /* Codifica o quadro */
            char *sendFrame = encode16((uint8_t *) frameAck, (((uint32_t) (112 / 8))));
            if(send(args.sock, (char *) sendFrame, 0x000e, 0) == -1)
            {
                printf("Erro! ao reenviar confirmacao.\n");
                return NULL;
            }
            free(sendFrame);
            free(convFrame);
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
        if(send(args.sock, (char *) args.frame, strlen((char *) args.frame), 0) == -1)
        {
            printf("Erro! ao reenviar quadro.\n");
            pthread_exit(NULL);
            return NULL;
        }
        sleep(1);
    }

    return NULL;
}

char *encode16(uint8_t *buffer, uint32_t nsize)
{
    char *coded = (char *) malloc(2 * nsize * sizeof(char));
    memset(coded, 0x00, (2 * nsize));

    uint64_t j = 0;
    for(uint32_t i = 0; i < nsize; i++, j += 2)
    {
        //uint8_t value = buffer[i];
        //uint8_t first = (value & 0xf0) >> 4;
        //uint8_t second = (value & 0x0f); //<< 4;
        char value[3];
        sprintf(value, "%02x", buffer[i]);
        //sprintf(value, "%x", buffer[i]);
        coded[j] = value[0];
        coded[j + 1] = value[1];

        /*
        sprintf(&coded[j], "%x", first);
        sprintf(&coded[j + 1], "%x", second);
        j += 2;
        */
    }
    //coded[j] = '\0';

    return coded;
}

uint8_t *decode16(char *buffer, uint32_t csize)
{
    uint8_t *decoded = (uint8_t *) malloc((csize / 2) * sizeof(uint8_t));
    memset(decoded, 0x00, (csize / 2));

    uint64_t j = 0;
    for(uint32_t i = 0; i < (csize / 2); i++)    {
        char first = buffer[j];
        char second = buffer[j + 1];
        uint8_t nib1 = 0x00;
        uint8_t nib2 = 0x00;

        /* Primeiro nibble */
        if((first >= 'a') && (first <= 'f'))
            nib1 = (uint8_t) (first - 'a' + 10);
        else if((first >= 'A') && (first <= 'F'))
            nib2 = (uint8_t) (first - 'A' + 10);
        else if((first >= '0') && (first <= '9'))
            nib1 = (uint8_t) (first - '0');
        nib1 = (nib1 & 0x0f) << 4;
        
        /* Segundo nibble */
        if((second >= 'a') && (second <= 'f'))
            nib2 = (uint8_t) (second - 'a' + 10);
        else if((second >= 'A') && (second <= 'F'))
            nib2 = (uint8_t) (second - 'A' + 10);
        else if((second >= '0') && (second <= '9'))
            nib2 = (uint8_t) (second - '0');
        nib2 = (nib2 & 0x0f);

        decoded[i] = nib1 + nib2;

        j += 2;
    }

    return decoded;
}

uint16_t checksum(void *buffer, uint32_t bytes)
{
    uint32_t sum = 0;

    uint8_t *data = (uint8_t *) malloc(bytes * sizeof(uint8_t));
    memcpy(data, buffer, bytes);

    while(bytes > 1)    {
        sum += (*(uint16_t *) data)++;
        bytes -= 2;
    }

    if(bytes > 0)
        sum += *((uint8_t *) data);

    free(data);
    data = NULL;

    while(sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    return (~sum);
}

