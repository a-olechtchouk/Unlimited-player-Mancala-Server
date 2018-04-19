#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXNAME 80  /* maximum permitted name size, not including \0 */
#define NPITS 6  /* number of pits on a side, not including the end pit */
#define NPEBBLES 4 /* initial number of pebbles per pit */
#define MAXMESSAGE (MAXNAME + 50) /* initial number of pebbles per pit */

int port = 51021;
int listenfd; //this is our original socket fd that is used to accept new connections.
int num_players = 0;

char gamestate[MAXMESSAGE];

struct player {
    int fd;    //this will be the socket fd we use for communication b/w client and server
    char name[MAXNAME+1]; 
    char buffer[MAXNAME+1];
    int pits[NPITS+1];  // pits[0..NPITS-1] are the regular pits 
                        // pits[NPITS] is the end pit
    //other stuff undoubtedly needed here (For the input of the player name only, if the data you read does not include a newline, you must loop to get the rest of the name.)
    int waiting_for_name;
    int user_dropped_connection;
    int readbytes;
    int pit_selected;

    struct player *next;
};
//playerlist is similar to the connections array, is a pointer to all the posible players.
struct player *playerlist = NULL;
//i think i should set playerlist to point to the first user joined, intially,
//and then move the pointer when a turn changes, OR if a player disconnects while it was his turn.


extern void parseargs(int argc, char **argv);
extern void makelistener();
extern int compute_average_pebbles();
extern int game_is_over();  /* boolean */
extern void broadcast(char *s, struct player *not_included_player);  /* you need to write this one */



/* Accept a connection. Note that a new file descriptor is created for
 * communication with the client. The initial socket descriptor is used
 * to accept connections, but the new socket is used to communicate.
 * Return the new client's file descriptor or -1 on error.
 */
int accept_connection(int fd, struct player *new_player) {
    
    int client_fd = accept(fd, NULL, NULL); //client fd is the new socket we can communicate with
    if (client_fd < 0) {
        perror("server: accept");
        close(fd);
        exit(1);
    }
    char *welcome = "Welcome to Mancala. What is your name?\r\n";
    write(client_fd, welcome, strlen(welcome));

    //dont get name of user here, since they might take a while to enter their name.

    new_player->fd = client_fd;
    //connections[user_index].sock_fd = client_fd;
    //connections[user_index].username = NULL;        //this username is a pointer to char
    return client_fd;
    
}

//computes the gamestate of a valid player p.
void game_state(struct player *p){
    
    //set all character to \0 
    memset(gamestate, '\0', sizeof(gamestate));

    char *end_pit = "[end pit]";
    
    //if the player is a valid player, create their gamestate
    if (p->waiting_for_name == 0 && p->user_dropped_connection == 0){
        //MAXNAME+1 since we want a : and a space
        strncpy(gamestate, p->name, MAXNAME + 2);
        int name_length = strlen(p->name);
        gamestate[name_length] = ':'; 
        gamestate[name_length+1] = ' ';

        
        //temporary variable for storing the integers into this string
        char nums[1028];
        //adds the pit information of a player.
        for (int i = 0; i < NPITS; i++){
            // pit_num = strtol(i, NULL, 10);
            // pebbles = strtol(p->pits[i], NULL, 10);
            strncat(gamestate, "[", 1);
            sprintf(nums, "%d", i);
            strncat(gamestate, nums, strlen(nums));
            strncat(gamestate, "]", 1);
            sprintf(nums, "%d", p->pits[i]);
            strncat(gamestate, nums, strlen(nums));
            //space
            strncat(gamestate, " ", 1);
        }
        //now add the endpit info
        strncat(gamestate, end_pit, strlen(end_pit));
        sprintf(nums, "%d", p->pits[NPITS]);
        strncat(gamestate, nums, strlen(nums));

        //add \r\n to the end of the message
        for (int i = 0; i < MAXMESSAGE - 1;i++){
            if (gamestate[i] == '\0'){
                gamestate[i] = '\r';
                gamestate[i+1] = '\n';
                break;
            }
        }
    }
    else{
        memset(gamestate, '\0', sizeof(gamestate));
    }
    //this person isnt a valid player, however this should never execute as we always will
    //pass in a valid player. just for safety.
}


int main(int argc, char **argv) {
    char msg[MAXMESSAGE];

    parseargs(argc, argv);
    makelistener();

    // The client accept - message accept loop. First, we prepare to listen 
    // to multiple file descriptors by initializing a set of file descriptors.
    int max_fd = listenfd;
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(listenfd, &all_fds);

    

    //curr_player is the player who's turn it is.
    struct player *curr_player;
   
    //new_player is the new player we add to the linked list when we accept a connection.
    struct player *new_player;
    int avg_pebbles = 0;
    while (!game_is_over()) {

        fd_set listen_fds = all_fds; //copy of our set of fd's

        //number of fd's that are ready for communication
        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);   
        if (nready == -1){
            perror("server: select");
            exit(1);
        }

        // Is it the original socket? Create a new connection for a client.
        
        if (FD_ISSET(listenfd, &listen_fds)) {

            //we find the average number of pebbles per pit for a new player, before they join.
            avg_pebbles = compute_average_pebbles();

            new_player = malloc(sizeof(struct player)); 
            
            new_player->fd = -1;
            new_player->waiting_for_name = 1;
            new_player->next = NULL;
            new_player->user_dropped_connection = 0;
            new_player->readbytes = 0;
            new_player->pit_selected = -1;
            memset(new_player->name, '\0', sizeof(new_player->name));

            int client_fd = accept_connection(listenfd, new_player);
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }

            FD_SET(client_fd, &all_fds);
            printf("Accepted a connection from a player.\n");

            new_player->fd = client_fd;

            //add the new player to the front of the linked list and update pointers
            if (playerlist != NULL){
                new_player->next = playerlist;
            }
            //since playerlist is NULL, then this is our first ever player,
            //and our first ever player (or oldest player, if the first one disconnects) always points to NULL
            else{
                //the first player ALWAYS points to NULL!!
                new_player->next = NULL;
            }

            playerlist = new_player;
        
        }


        
        for (struct player *p = playerlist; p; p = p->next) {

            if (curr_player == NULL){
                curr_player = playerlist;
            }
           

            //for all the players, check if they are trying to write to us.
            if (FD_ISSET(p->fd, &listen_fds)){

                //server is waiting for there name input to be filled.
                if (p->waiting_for_name){
                    int nbytes = read(p->fd, p->buffer, MAXNAME);

                    //client disconnected.
                    if (nbytes < 0){
                        perror("read");
                    }
                    else if (nbytes == 0){
                        //gotta fix dis up stilios
                        printf("%s\n", "A user has dropped connection while writing their name.\n");
                        p->user_dropped_connection = 1;
                        
                    }
                    //we have read data!
                    else{

                        int i;
                        for (i = 0; i < nbytes; i++){
                            if ((p->buffer)[i] == '\r' || (p->buffer)[i] == '\n'){
                                //strncat(p->name, &(p->buffer)[i], 1);
                                break;
                            }
                            else{
                                strncat(p->name, &(p->buffer)[i], 1);
                            }
                        }
                  
                        p->readbytes = p->readbytes + nbytes;
                        
                        //null terminate the endpoint 
                        (p->name)[p->readbytes - 1] = '\0';

                        //strncat null terminates for us, so we're okay.

                        //as long as p->name is not only \r,\n, \0's, then we're fine.
         
                        //int curr_bytes = nbytes + p->readbytes;
                        
                        //variable that checks to see if the name has characters that are not newlines.
                        int contains_regular_characters = 0;
                        for (int i = 0; i < strlen(p->name); i++){
                            if ((p->name)[i] != '\r' && (p->name)[i] != '\n' && (p->name)[i] != '\0'){
                                contains_regular_characters = 1;
                                break;
                            }
                            
                        }
                        

                        int same_name = 0;
                        for (struct player *player = playerlist; player; player = player->next){
                            if (player != p && strcmp(player->name,p->name) == 0){
                                    printf("%s\n", "A joining user is about to have the same name.");
                                    same_name = 1;
                                }
                            }

                        if (contains_regular_characters){
                            if (!same_name){
                                printf("%s\n", "User has a valid name." );

                                p->waiting_for_name = 0;
                                for (int i =0; i < NPITS; i++){
                                    p->pits[i] = avg_pebbles;
                                }
                                
                                //we can broadcast the gamestate to all valid players!
                                for (struct player *player = playerlist; player; player = player->next){
                                    game_state(player);
                                    broadcast(gamestate, NULL);
                                }

                                if (curr_player == NULL){
                                    curr_player = p;
                                    //curr_player = playerlist;
                                }
                                
                                if (p == curr_player){
                                    char *your_move = "Your move?\r\n";
                                    write(p->fd, your_move, strlen(your_move));
                                }
       
                            }
                            else{
                                p->waiting_for_name = 1;
                            }
         
                        }
                        else{
                            p->waiting_for_name = 1;
                        }
                    }

                    //Broadcast to all players except for curr_player, who's turn it is.
                    memset(msg, '\0', sizeof(msg));
                    strncpy(msg, "It is ", 6 );

                    //plus one for null terminator?
                    strncat(msg, curr_player->name, strlen(curr_player->name) + 1);
                    //including null term.
                    strncat(msg, "\'s move.\r\n", 11 );

                    //broadcast "It is p's move." to all valid players except for the current player.
                    broadcast(msg, curr_player);
                }

                //server is now NOT not waiting for name.
                else{

                    int nread = read(p->fd, msg, MAXMESSAGE);

                    if (nread == -1){
                        perror("read");
                    }
                    //client had a name and disconnected
                    else if (nread == 0){
                        //player disconnected :(
                        printf("%s\n", "A user has dropped connection during their turn." );
                        p->user_dropped_connection = 1;

                        if (p == curr_player){
                            curr_player = curr_player->next;
                        }

                    }

                    else{

                        //null terminate the data received, to make sure its a string.
                        if (!p->user_dropped_connection){
                            msg[MAXMESSAGE-1] = '\0';

                            int pitnum;
                            //if we didnt read a correct number, set pitnum to -1.
                            for (int i = 0; i < nread; i++){
                                if (msg[i] == '\0' || msg[i] == '\n' || msg[i] == '\r' ){
                                    pitnum = -1;
                                }
                                else{
                                    pitnum = 0;
                                    break;
                                }

                            }

                            if (pitnum != -1){
                                pitnum = strtol(msg, NULL, 10);
                                printf("%d\n", pitnum);
                            }
                            
          
                            if (pitnum < 0 || pitnum >= NPITS || p->pits[pitnum] == 0 || pitnum == -1){

                                if (p == curr_player){
                                    char *invalid_pit = "Invalid pit.\r\n";
                                    write(p->fd, invalid_pit, strlen(invalid_pit));
                                    curr_player->pit_selected = -1;
                                }
                                
                            }

                            else{
                                p->pit_selected = pitnum;
                            }

                            if (curr_player == NULL){
                                curr_player = playerlist;
                            }

                            printf("Current player is %s\n", curr_player->name );

                            if (curr_player == p){

                                if (!curr_player->waiting_for_name && !curr_player->user_dropped_connection){

                                    //start to move pebbles around the board.

                                    //the number of pebbles that are in the pit of the player selected
                                    int pitselected = p->pit_selected;
                                    int num_pebbles = curr_player->pits[pitselected];
                                    int repeat_turn = 0; 

                                    //since we pick up all the pebbles, set the pebbles in the pit selected to 0.
                                    if (pitselected != -1){
                                        curr_player->pits[pitselected] = 0;

                                        //first fill our board first. if amount of pebbles dont exceed our end pit,
                                        //break out of this for loop and start to move pebbles to other player's boards.
                                        //i is initially pit_num + 1 since we dont want to put a pebble in the pit we select.
                                        int pebbles_in_end_pit = curr_player->pits[NPITS];

                                        int num_pebbles_printing = num_pebbles;

                                        printf("About to move %d pebbles\n", num_pebbles);

                                        for (int i = pitselected + 1; i <= NPITS; i++){
                                                if (num_pebbles >= 1){
                                                    curr_player->pits[i] = curr_player->pits[i] + 1;
                                                    num_pebbles--;
                                                }

                                                //if our last pebble has ended in our endpit, we get another turn. 
                                                
                                                if (num_pebbles == 0 && curr_player->pits[NPITS] == pebbles_in_end_pit + 1){
                                                    printf("%s\n", "The last pebble went in our endpit" );
                                                    repeat_turn = 1;
                                                    break;
                                                }
                                        }

                                        //rcv_p is receiving player, the player who will receive pebbles in their board.
                                        struct player *rcv_p = curr_player->next;
                                    
                                        //while we still have pebbles leftover
                                        printf("There are pebbles leftover %d\n", num_pebbles);
                                        while (num_pebbles > 0){


                                            //if we have ended up at the oldest player, who's next attribute points to NULL,
                                            //since we have to wrap around the board, set it to be the head (playerlist).
                                            if (rcv_p == NULL){
                                                rcv_p = playerlist;
                                            }

                                            //we keep skipping players who have either disconnected, or are still waiting for a name.
                                            while ((rcv_p->user_dropped_connection == 1 || rcv_p->waiting_for_name == 1) && rcv_p != NULL){
                                                rcv_p = rcv_p->next;
                                                if (rcv_p == NULL){
                                                    rcv_p = playerlist;
                                                }
                                            }
                                            
                                            //if we have wrapped around the board, and we can fill our own endpit.
                                            if (curr_player == rcv_p){
                                                for (int i = 0; i <= NPITS; i++){
                                                    if (num_pebbles >= 1){
                                                        curr_player->pits[i] = curr_player->pits[i] + 1;
                                                        num_pebbles--;
                                                    }
                                                   
                                                    if (num_pebbles == 0 && i == NPITS){
                                                        repeat_turn = 1;
                                                        break;
                                                    }
                                                }
                                            }
                                            //the receiving player is not the current player who made the turn.
                                            else{
                                                for (int i = 0; i < NPITS;i++){
                                                    if (num_pebbles >= 1){
                                                        rcv_p->pits[i] = rcv_p->pits[i] + 1;
                                                        num_pebbles--;
                                                    }
                                                }
                                            }
                                        }

                                        printf("%s\n", "All pebbles have been moved." );


                                        memset(msg, '\0', sizeof(msg));

                                        snprintf(msg, MAXMESSAGE, "The player has moved %d pebbles.\r\n", num_pebbles_printing);
                                       
                                        //since a valid turn has been made, we can broadcast the new gamestate
                                        // to all valid players.
                                        for (struct player *player = playerlist; player; player = player->next){
                                            game_state(player);
                                            broadcast(gamestate, NULL);   
                                        } 
                                        broadcast(msg, NULL);

                                    }

                                    else{
                                        //they selected an invalid pit.
                                        repeat_turn = 1;
                                    }
                        
                                    //we can now set the turn of the player to the next player,
                                    //making them the current player.
                                    if (curr_player->user_dropped_connection){
                                        curr_player = curr_player->next;
                                    }

                                    if (repeat_turn == 0){
                                        curr_player = curr_player->next;
                                        //if the next player is NULL, then we know we should go to the head of the list (playerlist).
                                        if (curr_player == NULL){
                                            curr_player = playerlist;
                                        }
                                    }

                                    if (curr_player && !game_is_over()){
                                        char *your_move = "Your move?\r\n";
                                        write(curr_player->fd, your_move, strlen(your_move));
                                    }
                                }
                            }

                            // (curr_player != p)
                            else{
                                char *invalid_turn_string = "It is not your move.\r\n";
                                write(p->fd, invalid_turn_string, strlen(invalid_turn_string));
                            }
                        }
                    }
                }
            }
        } 


        //dc stands for disconnected player.
        struct player *dc;
        //temp is our temporary player.
        struct player *tempp;
        int player_was_deleted = 0;
        for (struct player *p = playerlist; p; p=p->next){

            if (p->user_dropped_connection){
                dc = p;
                player_was_deleted = 1;
            }
            
            if (player_was_deleted){

                if (p == playerlist){
                    playerlist = playerlist->next;
                    printf("Deallocated player: %s\n", dc->name );
                    if (dc != NULL){
                        FD_CLR(dc->fd, &all_fds);
                        free(dc);   
                        break;
                    }
                }
                else{

                    if (p->next == NULL){
                        tempp->next = NULL;
                    }
                    else{
                        tempp->next = (tempp->next)->next;
                    }

                    if (dc != NULL){
                        printf("Deallocated player: %s\n", dc->name );
                        FD_CLR(dc->fd, &all_fds);
                        free(dc);
                        break;
                    }
                }
            }
            else{
                tempp = p;
            }
         }
    }  

    close(listenfd);
    memset(msg, '\0', sizeof(msg));
    char *game_over = "Game over!\r\n";
    strncpy(msg, game_over, strlen(game_over));
    broadcast(msg, NULL);

    printf("Game over!\n");

    for (struct player *p = playerlist; p; p = p->next) {
        int points = 0;
        for (int i = 0; i <= NPITS; i++) {
            points += p->pits[i];
        }

        printf("%s has %d points\n", p->name, points);
        memset(msg, '\0', sizeof(msg));
        snprintf(msg, MAXMESSAGE, "%s has %d points\r\n", p->name, points);
        broadcast(msg, NULL);
    }

    return 0;
}

//broadcasts msg to all valid players, except for not_included player, if there is one.
void broadcast(char *msg, struct player *not_included_player){
    //write msg to all valid players p
    for (struct player *p = playerlist; p; p = p->next){
        if ((p->waiting_for_name == 0 && p->user_dropped_connection == 0)){
            if (p != not_included_player){
                write(p->fd, msg, MAXMESSAGE);
            }
        }
    } 
}

void parseargs(int argc, char **argv) {
    int c, status = 0;
    while ((c = getopt(argc, argv, "p:")) != EOF) {
        switch (c) {
        case 'p':
            port = strtol(optarg, NULL, 0);  
            break;
        default:
            status++;
        }
    }
    if (status || optind != argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        exit(1);
    }
}


void makelistener() {
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    int on = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
               (const char *) &on, sizeof(on)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&r, sizeof(r))) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
}


//might need to change this in case of disconnected players.
/* call this BEFORE linking the new player in to the list */
int compute_average_pebbles() { 
    struct player *p;
    int i;

    if (playerlist == NULL) {
        return NPEBBLES;
    }

    int nplayers = 0, npebbles = 0;
    for (p = playerlist; p; p = p->next) {
        if (p->waiting_for_name == 0 && p->user_dropped_connection == 0){
            nplayers++;
            for (i = 0; i < NPITS; i++) {
                npebbles += p->pits[i];
            }
        }
        
    }
    if (nplayers == 0){
        return NPEBBLES;
    }
    return ((npebbles - 1) / nplayers / NPITS + 1);  /* round up */
}


int game_is_over() { /* boolean */
    int i;

    if (!playerlist) {
       return 0;  /* we haven't even started yet! */
    }

    for (struct player *p = playerlist; p; p = p->next) {
        if (p->waiting_for_name == 0 && p->user_dropped_connection == 0){
            int is_all_empty = 1;
            for (i = 0; i < NPITS; i++) {
                if (p->pits[i]) {
                    is_all_empty = 0;
                }
            }
            if (is_all_empty) {
                return 1;
            }
        }
        
    }
    return 0;
}
