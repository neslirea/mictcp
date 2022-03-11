#include <mictcp.h>
#include <api/mictcp_core.h>

/* Variables globales */
mic_tcp_sock mysock;
mic_tcp_sock_addr addr_sock_dest;

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm){
  printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");

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
    // int IP_send(mic_tcp_pdu pk, mic_tcp_sock_addr addr)
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    if(mysock.fd == mic_sock){ // Si le socket a bien été créé correctement 
        // 1 - Construction du PDU
        mic_tcp_pdu pdu;
        mic_tcp_header header;
        mic_tcp_payload payload;
        
        header.source_port = mysock.addr.port; /* numéro de port source */
        header.dest_port = addr_sock_dest.port; /* numéro de port de destination */
        header.seq_num = 0; /* numéro de séquence */
        header.ack_num = 0; /* numéro d'acquittement */
        header.syn = 0; /* flag SYN (valeur 1 si activé et 0 si non) */
        header.ack = 0; /* flag ACK (valeur 1 si activé et 0 si non) */
        header.fin = 0; /* flag FIN (valeur 1 si activé et 0 si non) */

        payload.data = mesg;
        payload.size = mesg_size;

        pdu.header = header;
        pdu.payload = payload;

        // 2 - Envoyer le PDU à la couche IP
        IP_send(pdu, addr_sock_dest);

        // 3 - Attente d'un ACK (V2)
        return 0;
    }
    else {
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
    mic_tcp_payload payload; // creation contenu de pdu
    payload.data = mesg;
    payload.size = max_mesg_size;
  
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    if(mysock.fd == socket && mysock.state == ESTABLISHED){ // Si le socket a bien été créé correctement et que le sock est en état connecté

      /* Attente d'un PDU */
      mysock.state = WAIT_FOR_PDU;

      /* Recuperation d'un PDU dans le buffer de reception */
      nb_octets_lus = app_buffer_get(payload);

      mysock.state = ESTABLISHED;
      return nb_octets_lus;  
    }

    else{
      return -1;
    }
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
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    // V1 pas de mise à jour des numéros de séquence et d'acquittement
    
    app_buffer_put(pdu.payload); // insertion des données utiles dans le buffer de réception du socket

    mysock.state = ESTABLISHED;
}
