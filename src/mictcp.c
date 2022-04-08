#include <mictcp.h>
#include <api/mictcp_core.h>

/* Variables globales */
mic_tcp_sock mysock;
mic_tcp_sock_addr addr_sock_dest;
int PA = 0; // prochain acquittement attendu
int PE = 0; // prochaine emission attendue
const int max_envoi = 10;
const float pourcentage_perte = 10.0f;

// fenetre glissante
const int taille_fenetre = 20;
short *tab;
int courant;

int nb_perdus();
float pourcentage_perte_actuel();

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
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    if(mysock.fd == socket){ // Si le socket a bien été créé correctement
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
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    if(mysock.fd == socket){ // Si le socket a bien été créé correctement
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
    unsigned long timeout = 100; //100 ms
    
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

        // v3.2
        /*
        if(nb_pdu_env == 50){
          nb_pdu_env = 0;
          nb_pdu_perdus = 0;
        }*/

        // 2 - Envoi du PDU à la couche IP
        int octets_env = IP_send(pdu, addr_sock_dest);
        nb_pdu_env++;

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
            //printf("ACK BIEN RECU\n");
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
    printf("[MIC-TCP] Appel de la fonction :  "); printf(__FUNCTION__); printf("\n");
    return -1;
}

/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put().
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_sock_addr addr)
{
  mic_tcp_pdu ack; // Création de l'ACK

  //printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");

  if (pdu.header.seq_num == PA) { // DT.n°seq == PA
    app_buffer_put(pdu.payload); // Ajout de la charge utile du PDU recu dans le buffer de reception 
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
 