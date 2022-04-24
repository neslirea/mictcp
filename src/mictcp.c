#include <mictcp.h>
#include <api/mictcp_core.h>
#include <pthread.h> // librairie pour les variables de threads
 // librairie pour détruire socket

/* Variables globales */
mic_tcp_sock mysock;
mic_tcp_sock_addr addr_sock_dest;
int PA = 0; // prochain acquittement attendu
int PE = 0; // prochaine emission attendue

/* Varialbes liées aux pertes */
const int max_envoi = 10; // nb envoi max avant abandon
int pourcentage_perte = 0; // perte acceptee pour le Stop & Wait (valeur donnée statiquement lors de la création du socket)
int perte_admissible; // valeur de perte tolérée à la fin de la négociation
unsigned long timeout = 100; //100 ms

/* Fenêtre glissante */
const int taille_fenetre = 20;
short *tab;
int courant;

int nb_perdus();
float pourcentage_perte_actuel();

/* Mutex et variable de condition */
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm){
  printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
  /* fenetre glissante */
  tab = malloc(sizeof(short)*taille_fenetre);
  courant = 0;
  for (int i=0; i<taille_fenetre;i++){
      tab[i]=0;
  }

  /* Pourcentage de pertes sur le reseau */
  set_loss_rate(5);

  /* Client */
  if (sm == CLIENT) {
    pourcentage_perte=20;
    printf("Pourcentage de perte admissible côté client : %d %%\n", pourcentage_perte);
  }
  /* Serveur */
  else {
    pourcentage_perte=10;
    printf("Pourcentage de perte admissible côté serveur : %d %%\n", pourcentage_perte);
  }

  if(initialize_components(sm)!=-1){ /* Appel obligatoire */
    /* 
     * if sm == SERVEUR
     *    1 - Creation du socket local + bind 
     *    2 - adressage du socket --> construction de l'@
     *                            '--> association @ et socket via BIND
     * if sm == CLIENT
     *    1 - creation socket local
     *    2 - construction de l'@ local
     *    3 - construction @ à atteindre
     */

    mysock.fd = 0; // identificateur du socket
    mysock.state = IDLE; // etat du socket
    return mysock.fd;
  }
  else {
    return -1 ;
  }
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr){
   printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
   if (mysock.fd==socket){ // Si le socket a été créé correctement
    memcpy((char*)&mysock.addr, (char*)&addr, sizeof(mic_tcp_sock_addr)); // mysock.addr=addr --> on attribue l'adresse mise en paramètre au socket
    return 0;
  }
  else{
    return -1;
  }
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr){
  /* Ici nous nous plaçons du côté du serveur qui va accepter la primitive connect() 
   * et donc attendre une initialisation de la phase de connexion de la part du client avec un SYN.
   * Le serveur enverra alors un SYN ACK et se mettra en état d'attente d'un ACK pour finaliser la phase d'établissement du connexion
   */
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    if(mysock.fd == socket){ // Si le socket a bien été créé correct
      printf("ATTENTE DE RECEPTION SYN \n");

      mysock.state = WAIT_FOR_SYN; // Mise en état d'attente de réception d'un PDU SYN

      /* Attente de réception du PDU SYN de la part du client */
      if(pthread_mutex_lock(&mutex) != 0){
        printf("Erreur mutex lock");
      } 
      pthread_cond_wait(&cond,&mutex); // attente de réception d'un signal pour se réveiller     
      if(pthread_mutex_unlock(&mutex) != 0){
        printf("Erreur mutex unlock");
      }

      printf("RECEPTION DU SYN\n");

      /* Création du SYN ACK */
      mic_tcp_pdu SYN_ACK; 

      /* Construction du SYN ACK */
      SYN_ACK.header.source_port = mysock.addr.port;
      SYN_ACK.header.dest_port = addr->port;
      SYN_ACK.header.syn = 1;
      SYN_ACK.header.ack = 1;
      SYN_ACK.header.ack_num = pourcentage_perte; // Le serveur envoie le pourcentage de perte qu'il tolère au maximum
      SYN_ACK.payload.size = 0; // on n'envoie pas de donnée    

      /* Envoi du PDU SYN ACK */
      IP_send(SYN_ACK, *addr); 

      printf("ENVOI DU SYN ACK ET ATTENTE DE L'ACK\n");
      mysock.state = WAIT_FOR_ACK; // Mise en état d'attente de réception d'un PDU ACK   

      /* Attente de réception du PDU ACK de la part du client */
      if(pthread_mutex_lock(&mutex) != 0){
        printf("Erreur mutex lock");
      } 
      pthread_cond_wait(&cond,&mutex); // attente de réception d'un signal pour se réveiller     
      if(pthread_mutex_unlock(&mutex) != 0){
        printf("Erreur mutex unlock");
      }

      printf("ACK RECU\n");

      printf("Negociation du taux de perte admissible : %d %%\n", perte_admissible);

      printf("------ PHASE DE CONNEXION TERMINEE ------\n");

      mysock.state = ESTABLISHED; // On met le socket dans l'état connecté
      return 0;
    }
    else{
        return -1;
  }
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr){
    mic_tcp_pdu SYNACK;
    int synack_recu = 0;
    /* mic_tcp_connect est utilisée par le client donc pourcentage_perte correspondra ici au pourcentage toléré par le client */
    int pourcentage_perte_client = pourcentage_perte;
    int pourcentage_perte_serveur = 0;
    
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    if(mysock.fd == socket){ // Si le socket a bien été créé correctement
    printf("------ INITIALISATION DE LA PHASE D'ETABLISSEMENT DE CONNEXION ------\n");
      /* Construction du SYN */
      mic_tcp_pdu SYN;
      SYN.header.syn = 1;
      SYN.header.source_port = mysock.addr.port; /* numéro de port source */
      SYN.header.dest_port = addr_sock_dest.port; /* numéro de port destination */
      SYN.payload.size = 0; // on n'envoie pas de donnée

      /* Envoi du PDU SYN */
      if(IP_send(SYN, addr) == -1){
        printf("ERREUR IP_send du SYN\n");
        exit(EXIT_FAILURE); 
      } 

      printf("ENVOI DU SYN ET ATTENTE DU SYN ACK");
      mysock.state = WAIT_FOR_SYN_ACK; // Mise en état d'attente de réception d'un PDU SYN ACK

      /* Attente de réception du SYN ACK de la part du serveur */
      while(synack_recu == 0){ // Tant qu'on n'a pas reçu le SYN ACK
        // SI le SYN ACK que l'on reçoit -->   n'a pas timeout   --ET--   est bien le SYN ACK attendu
        if((IP_recv(&SYNACK, &addr_sock_dest, timeout) != -1) && (SYNACK.header.ack == 1) && (SYNACK.header.syn == 1)){
          /* Si le PDU reçu respecte toutes ces conditions nous pouvons sortir du while */
          synack_recu = 1;
          pourcentage_perte_serveur = SYNACK.header.ack_num;
          printf("RECEPTION DU SYN ACK\n");
        }
        else{
          // timeout expiré
          printf("TIMEOUT DU PDU SYN --> REEMISSION DU PDU SYN\n");
          IP_send(SYN, addr); 
        }
      } 
  
      /* Negociation du pourcentage de pertes admissibles entre le client et le serveur */
      /* On garde le pourcentage le plus faible */
      if(pourcentage_perte_serveur > pourcentage_perte_client) {
        perte_admissible = pourcentage_perte_client; 
      } 
      else {
          perte_admissible = pourcentage_perte_serveur;
      }
      printf("Negociation du taux de perte admissible : %d %%\n", perte_admissible);

      /* Construction du PDU ACK */
      mic_tcp_pdu ACK;
      ACK.header.ack = 1;
      ACK.header.source_port = mysock.addr.port; // numéro de port source
      ACK.header.dest_port = addr_sock_dest.port; // numéro de port destination
      ACK.header.seq_num = perte_admissible; // Le client envoie au serveur la perte admissible finale
      ACK.header.ack_num = -1; // valeur pour vérifier dans le receive que c'est bien l'ACK de l'etablissement de la connexion
      ACK.payload.size = 0; // on n'envoie pas de donnée
      /* Envoi du PDU ACK */
      if(IP_send(ACK, addr) == -1){
        printf("ERREUR IP_send de l'ACK\n");
        exit(EXIT_FAILURE); 
      } 

      mysock.state = ESTABLISHED; // On met le socket dans l'état connecté
      addr_sock_dest = addr; // On attribue l'adresse mise en paramètre au socket distant (serveur)
      return 0;
    }
    else {
      return -1;
    }
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size){
    int ack_recu = 0;
    mic_tcp_pdu pdu;
    mic_tcp_pdu ack;
    
    //printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    if(mysock.fd == mic_sock && mysock.state == ESTABLISHED){ // Si le socket a bien été créé correctement 
        // 1 - Construction du PDU à émettre
        pdu.header.source_port = mysock.addr.port; /* numéro de port source */
        pdu.header.dest_port = addr_sock_dest.port; /* numéro de port de destination */
        pdu.header.seq_num = PE; /* numéro de séquence */ 
        pdu.header.syn = 0; /* flag SYN (valeur 1 si activé et 0 si non) */
        pdu.header.ack = 0; /* flag ACK (valeur 1 si activé et 0 si non) */
        pdu.header.fin = 0; /* flag FIN (valeur 1 si activé et 0 si non) */

        PE = (PE+1)%2; // Mise à jour de PE

        pdu.payload.data = mesg;
        pdu.payload.size = mesg_size;

        // 2 - Envoi du PDU à la couche IP
        int octets_env = IP_send(pdu, addr_sock_dest);

        // 3 - Attente d'un ACK
        mysock.state = WAIT_FOR_ACK;

        // Construction ACK
        ack.payload.size = 0;
        int nb_env=0;

        while(ack_recu == 0){ // Tant qu'on n'a pas reçu d'ACK
          // SI l'ACK que l'on reçoit -->   n'a pas timeout   --ET--   est bien un ACK   --ET--   ACK.ack = PE
          if((IP_recv(&ack, &addr_sock_dest, timeout) != -1) && (ack.header.ack == 1) && (ack.header.ack_num == PE)){
            /* 
             * Si l'ACK reçu respecte toutes ces conditions on dit qu'on a reçu le bon ACK et que donc nous pouvons sortir du while
             * afin d'envoyer la prochaine trame à émettre (s'il y en a une)
             */
            ack_recu = 1;
            //on update la fenêtre
            tab[courant]=1;
            mysock.state = ESTABLISHED;
          }
          else{ // SINON --> On renvoie le PDU tout en imposant une limite maximale d'envoi pour le même PDU
            //update fenetre
            tab[courant]=0;
            //printf("taille fenetre :%d, pourcentage_perte_actuel : %0.2f, pourcentage_perte : %0.2f\n", taille_fenetre, pourcentage_perte_actuel(), pourcentage_perte);
            if(pourcentage_perte_actuel() > pourcentage_perte && nb_env<max_envoi){
              printf("---------- REPRISE DE LA PERTE ------------ \n");
              octets_env = IP_send(pdu, addr_sock_dest);
              nb_env++;
            } 
            else{              
              ack_recu = 1;
              mysock.state = ESTABLISHED;
            }
          } 
        }

        // on incrémente l'index courant de la fenetre
        courant=(courant+1)%taille_fenetre; 
        return octets_env;
    }
    else{
      return -1;
    }
}

/*
 * Permet à l’application réceptrice de réclamer la récupération d’une donnée
 * stockée dans les buffers de réception du socket
 * Retourne le nombre d’octets lu ou bien -1 en cas d’erreur
 * NB : cette fonction fait appel à la fonction app_buffer_get()
 */
int mic_tcp_recv (int socket, char* mesg, int max_mesg_size)
{
    int nb_octets_lus;
    mic_tcp_payload pdu; // creation contenu de pdu
    pdu.data = mesg;
    pdu.size = max_mesg_size;
  
    //printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    if(mysock.fd == socket && mysock.state == ESTABLISHED){ // Si le socket a bien été créé correctement et que le sock est en état connecté
      /* Recuperation d'un PDU dans le buffer de reception */
      nb_octets_lus = app_buffer_get(pdu);
      
      mysock.state = ESTABLISHED;
      
      return nb_octets_lus;  
    }
    return -1;
}

/*
 * Permet de réclamer la destruction d’un socket.
 * Engendre la fermeture de la connexion suivant le modèle de TCP.
 * Retourne 0 si tout se passe bien et -1 en cas d'erreur
 */
int mic_tcp_close (int socket)
{
    int finack_recu = 0;
    int nb_env = 0;
    printf("[MIC-TCP] Appel de la fonction :  "); printf(__FUNCTION__); printf("\n");
    if(mysock.fd == socket && mysock.state == ESTABLISHED){ // Si le socket a bien été créé correctement et que le sock est en état connecté
      printf("------ INITIALISATION DE LA PHASE DE FERMETURE DE CONNEXION ------\n");
      
      /* Construction du FIN */
      mic_tcp_pdu FIN;
      FIN.header.fin = 1;
      FIN.header.source_port = mysock.addr.port; /* numéro de port source */
      FIN.header.dest_port = addr_sock_dest.port; /* numéro de port destination */
      FIN.payload.size = 0; // on n'envoie pas de donnée

      /* Envoi du PDU FIN */
      if(IP_send(FIN, addr_sock_dest) == -1){
        printf("ERREUR IP_send du FIN\n");
        exit(EXIT_FAILURE); 
      }
      nb_env++;

      mic_tcp_pdu finack;
      while(finack_recu == 0){ // Tant que l'on n'a pas reçu de finack de la part du serveur
        // SI l'ACK que l'on reçoit -->   n'a pas timeout   --ET--   est bien un ACK   --ET--   ACK.ack = PE
        if((IP_recv(&finack, &addr_sock_dest, timeout) != -1) && (finack.header.ack == 1) && (finack.header.fin == 1)){
          /* 
            * Si le finack reçu respecte toutes ces conditions on dit qu'on a reçu le bon finack et que donc nous pouvons sortir du while
            * afin d'envoyer la prochaine trame à émettre (s'il y en a une)
            */
          finack_recu = 1;
        }
        else{ // SINON --> On renvoie le PDU tout en imposant une limite maximale d'envoi pour le même PDU
          if(nb_env<max_envoi){
            printf("---------- REPRISE DE LA PERTE ------------ \n");
            IP_send(FIN, addr_sock_dest);
            nb_env++;
          } 
          else{              
            finack_recu = 1;
          } 
        }
      }

      /* Construction du ACK */
      mic_tcp_pdu ACK;
      ACK.header.fin = -1; // repère pour le serveur (pour qu'il sache que l'ACK reçu est celui de la fermeture de la connexion)
      ACK.header.ack = 1;
      ACK.header.source_port = mysock.addr.port; /* numéro de port source */
      ACK.header.dest_port = addr_sock_dest.port; /* numéro de port destination */
      ACK.payload.size = 0; // on n'envoie pas de donnée

      /* Envoi du PDU ACK */
      if(IP_send(ACK, addr_sock_dest) == -1){
        printf("ERREUR IP_send du FIN\n");
        exit(EXIT_FAILURE); 
      }

      printf("----- FERMETURE DE LA CONNEXION TERMINEE ----- \n");
      mysock.state == CLOSED;
      close(socket); // destruction socket
      return 0;
    } 

    return -1;
}

/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put().
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_sock_addr addr){
  mic_tcp_pdu ack; // Création de l'ACK

  //printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
  
  /* POUR LA PHASE D'ETABLISSEMENT DE CONNEXION */
  if(mysock.state == WAIT_FOR_SYN){ // Si le serveur est en état d'attente d'un PDU SYN
    /* Envoi du signal pour réveiller le serveur afin qu'il puisse envoyer le PDU SYN ACK */
    if (pthread_cond_signal(&cond) != 0) {
			printf("Erreur: pthread_cond_signal\n");
			}
  }

  else if(mysock.state == WAIT_FOR_ACK){ // Si le serveur est en état d'attente d'un ACK --> le seul cas est celui de la phase d'établissement de connexion dans notre sujet
    /* Envoi du signal pour réveiller le serveur afin qu'il dise que l'etablissement de la connexion s'est bien déroulé */
    perte_admissible = pdu.header.seq_num; // le serveur prend en compte la perte admissible finale
    if (pthread_cond_signal(&cond) != 0) {
			printf("Erreur: pthread_cond_signal\n");
			}
  }

  else if(pdu.header.fin == 1){ // Si le serveur reçoit un pdu ayant le "fin" à 1
    /* Il comprend que le client veut fermer la connexion et lui envoie alors un pdu "fin/ack" */
    // Construction du PDU FIN/ACK
    mic_tcp_pdu FINACK;
    FINACK.header.source_port = mysock.addr.port;
    FINACK.header.dest_port = addr.port;
    
    FINACK.header.fin = 1;
    FINACK.header.ack = 1;

    FINACK.payload.size = 0; // on n'envoie pas de donnée

    // Envoi de l'ACK à l'émetteur
    IP_send(FINACK, addr);

    mysock.state == CLOSING;
  }

  else if(mysock.state == CLOSING){ // Si le serveur est en état de fermeture de connexion et qu'il reçoit un paquet
    /* Il envoie le PDU FIN */
    // Construction du PDU ACK de fermeture de connexion
    ack.header.source_port = mysock.addr.port;
    ack.header.dest_port = addr.port;
    
    ack.header.ack = 1;

    ack.payload.size = 0; // on n'envoie pas de donnée

    // Envoi de l'ACK à l'émetteur
    IP_send(ack, addr);

    mysock.state == CLOSED;
  }

  else{
    if (pdu.header.seq_num == PA) { // DT.n°seq == PA
      app_buffer_put(pdu.payload); // Ajout de la charge utile du PDsynU recu dans le buffer de reception 
      PA = (PA + 1) % 2; // Mise à jour de PA
    }
    // sinon --> DT.n°seq != PA --> rejet de la DT DONC PA n'est pas mise à jour

    // Construction de l'ACK
    ack.header.source_port = mysock.addr.port;
    ack.header.dest_port = addr.port;
    ack.header.ack_num = PA;
    ack.header.syn = 0;
    ack.header.ack = 1;
    ack.header.fin = 0;

    ack.payload.size = 0; // on n'envoie pas de donnée

    // Envoi de l'ACK à l'émetteur
    IP_send(ack, addr);
  }
}

int nb_perdus(){
    int res=0;
    for (int i=0; i<taille_fenetre;i++){
        if(!(tab[i])){
            res++;
        }
    }
    return res;
}

float pourcentage_perte_actuel(){
    int res=0;
    for (int i=0; i<taille_fenetre;i++){
        if(!(tab[i])){
            res++;
        }
    }
    return (float)res/taille_fenetre*100.;
}
 
