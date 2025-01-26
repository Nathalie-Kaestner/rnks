#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define TIMEOUT_INTERVAL 300    // Timeout fr select (300 ms)
#define MAX_TIMEOUTS 3          // Maximal 3x Timeout bevor Weitergehen
#define BUFFER_SIZE 512         // Maximale Gr��e der Daten pro Paket 

// Struktur fr eine Anfrage
struct request {
    unsigned char ReqType;      // Anfragetyp (z.B. Hello, Close, Daten)
    unsigned long SeNr;         // Sequenznummer
    char data[BUFFER_SIZE];     // Die Daten der Datei, die bertragen werden
};


// Funktion zum Senden einer "Hello"-Nachricht
void send_hello(SOCKET sock, struct sockaddr_in6* remoteAddr) {
    struct request req;
    memset(&req, 0, sizeof(req));   // mit 0 initialisieren
    req.ReqType = 'H';              // 'H' f�r Hello
    req.SeNr = htonl(0);            // SeqNr 0 f�r Hello-Nachricht

    sendto(sock, (const char*)&req, sizeof(req), 0, (struct sockaddr*)remoteAddr, sizeof(*remoteAddr));
    printf("Hello-Nachricht gesendet.\n");
}

// Funktion zum Senden einer Close-Nachricht
void send_close(SOCKET sock, struct sockaddr_in6* remoteAddr, unsigned long seqNr) {
    struct request req;
    memset(&req, 0, sizeof(req));   // mit 0 initialisieren
    req.ReqType = 'C';              // 'C' f�r Close
    req.SeNr = htonl(0);            // SeqNr 0 f�r Close-Nachricht

    sendto(sock, (const char*)&req, sizeof(req), 0, (struct sockaddr*)remoteAddr, sizeof(*remoteAddr));
    printf("Sende Close-Nachricht. \n");
}

// Funktion zum Senden einer Datei
void send_file(SOCKET sock, struct sockaddr_in6* remoteAddr, FILE* file) {
    struct request req;
    memset(&req, 0, sizeof(req));   // mit 0 initialisieren
    req.ReqType = 'D';              // 'D' f�r Daten

    unsigned long seqNr = 1;  // Start mit 1 f�r Datenpakete

    // Senden der Hello-Nachricht zum Verbindungsaufbau
    send_hello(sock, remoteAddr);

    struct sockaddr_in6 senderAddr;             // Struktur zur Speicherung der Absenderadresse
    int senderAddrSize = sizeof(senderAddr);    // Variable zur Speicherung der Gr��e der Absenderadresse
    struct request ack_req;                     // Struktur f�r die Empfangsbest�tigung (ACK oder NACK)

    fd_set read_fds;                            // pr�ft, ob Daten auf dem Socket zum Lesen bereitstehen
    struct timeval timeout;                     // Struktur zur Festlegung des Timeout-Intervalls f�r select()

    // Warten auf Hello-Ack mit Timeout
    FD_ZERO(&read_fds);                         // Bereinige fd_set (keine Socjets in Sammlung)
    FD_SET(sock, &read_fds);                    // Hinzuf�gen Socket zum Set der Dateideskriptoren
    timeout.tv_sec = 0;                         // Sekunden auf 0
    timeout.tv_usec = TIMEOUT_INTERVAL * 1000;  // Timer in Mikrosekunden

    // ist socket bereit innerhalb der timeout-periode (300ms) ein hello-ack zu senden
    if (select(sock + 1, &read_fds, NULL, NULL, &timeout) > 0) {
        recvfrom(sock, (char*)&ack_req, sizeof(ack_req), 0, (struct sockaddr*)&senderAddr, &senderAddrSize);
        if (ack_req.ReqType != 'A') {   // Pr�fen, ob ACK empfangen wurde
            fprintf(stderr, "Fehler: Keine g�ltige Hello-Antwort.\n");
            return;
        }
        printf("Hello-Antwort empfangen.\n");
    }
    else {
        fprintf(stderr, "Fehler: Timeout beim Warten auf Hello-Antwort.\n");
        return;
    }

    // Datei Zeile f�r Zeile lesen und senden
    while (fgets(req.data, sizeof(req.data), file) != NULL) {   // bis Ende Datei erreicht wird
        req.SeNr = htonl(seqNr);                                // Umwandlung der Sequenznummer ins Netzwerkbyte-Format
        seqNr++;                                                // Sequenznummer erh�hen

        int timeouts = 0;                   // Anzahl Timeouts auf 0 setzen
        while (timeouts < MAX_TIMEOUTS) {   // Erneutes Senden bei Timeout (Stop-and-Wait)
            printf("Sende: SeqNr = %lu, Daten = %s\n", ntohl(req.SeNr), req.data);
            sendto(sock, (const char*)&req, sizeof(req), 0, (struct sockaddr*)remoteAddr, sizeof(*remoteAddr));

            // Warten auf Antwort (ACK/NACK)
            FD_ZERO(&read_fds);                         // Bereinige fd_set (keine Socjets in Sammlung)
            FD_SET(sock, &read_fds);                    // Hinzuf�gen Socket zum Set der Dateideskriptoren 
            timeout.tv_sec = 0;                         // Sekunden auf 0
            timeout.tv_usec = TIMEOUT_INTERVAL * 1000;  // Timer in Mikrosekunden

            // ist der socket bereit innerhalb der timeout-periode (300ms) Daten zu empfangen?
            if (select(sock + 1, &read_fds, NULL, NULL, &timeout) > 0) {
                struct request response;
                recvfrom(sock, (char*)&response, sizeof(response), 0, (struct sockaddr*)&senderAddr, &senderAddrSize);  // Antwort erhalten

                if (response.ReqType == 'N') {      // Falls NACK empfangen wurde
                    printf("NACK erhalten f�r SeqNr %lu. Sende erneut.\n", ntohl(response.SeNr));
                    seqNr--;  // Wiederhole die Sequenznummer
                }
                else if (response.ReqType == 'A') { // Falls ACK empfangen wurde
                    printf("ACK erhalten f�r SeqNr %lu\n", ntohl(response.SeNr));
                    break;  // n�chstes Paket
                }
            }
            else {
                printf("Timeout f�r SeqNr %lu. Versuch %d von %d.\n", ntohl(req.SeNr), timeouts + 1, MAX_TIMEOUTS);
                timeouts++; // Anzahl Timeouts erh�hen
            }
        }
    }
    // Sende eine Close-Nachricht
    send_close(sock, remoteAddr, seqNr);

    // Warten auf Close-Ack mit Timeout
    FD_ZERO(&read_fds);                         // Bereinige fd_set (keine Sockets in Sammlung)
    FD_SET(sock, &read_fds);                    // Hinzuf�gen Socket zum Set der Dateideskriptoren
    timeout.tv_sec = 0;                         // Sekunden auf 0
    timeout.tv_usec = TIMEOUT_INTERVAL * 1000;  // Timer in Mikrosekunden

    // Pr�fen, ob socket bereit innerhalb der timeout-Periode (300ms) ein Close-Ack zu empfangen
    if (select(sock + 1, &read_fds, NULL, NULL, &timeout) > 0) {
        recvfrom(sock, (char*)&req, sizeof(req), 0, (struct sockaddr*)&senderAddr, &senderAddrSize);
        if (req.ReqType != 'A') {   // Pr�fen, ob ACK empfangen wurde
            fprintf(stderr, "Fehler: Keine g�ltige Close-Antwort.\n");
            return;
        }
        printf("Close-Antwort empfangen.\n");
    }
    else {
        fprintf(stderr, "Fehler: Timeout beim Warten auf Close-Antwort.\n");
        return;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        // berprfen, ob ausreichend Argumente bergeben wurden
        fprintf(stderr, "Usage: %s <Filename> <Multicast-Adresse> <Port> <Window Size>\n", argv[0]);
        return 1;
    }

    const char* filename = argv[1];         // Name der zu sendenden Datei
    const char* multicast_addr = argv[2];   // Multicast-Adresse
    int port = atoi(argv[3]);               // Portnummer

    // Initialisieren von Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        // Fehler bei der Initialisierung von Winsock
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    // Erstellen eines Sockets f�r UDPv6
    SOCKET sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        // Fehler beim Erstellen des Sockets
        fprintf(stderr, "Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Initialisieren der Zieladresse (Empf�nger)
    struct sockaddr_in6 remoteAddr;                             // Struktur zur Speicherung der Empf�ngeradresse
    memset(&remoteAddr, 0, sizeof(remoteAddr));                 // Bytes der Struktur auf 0 setzen, damit keine unerwarteten Werte enthalten sind
    remoteAddr.sin6_family = AF_INET6;                          // Adressfamilie AF_INET6 (IPv6)
    remoteAddr.sin6_port = htons(port);                         // Umwandlung Portnummer in Netzwerkformat
    inet_pton(AF_INET6, multicast_addr, &remoteAddr.sin6_addr); // Konvertiere eingegebene IP-Adresse

    // ffne Datei
    FILE* file = NULL;
    if (fopen_s(&file, filename, "r") != 0 || file == NULL) {   // r = read, 0 hei�t erfolgreich ge�ffnet
        fprintf(stderr, "Datei %s konnte nicht ge�ffnet werden.\n", filename);
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    else {
        printf("Datei %s erfolgreich ge�ffnet.\n", filename); // Debug-Ausgabe
    }

    // Sende Datei
    send_file(sock, &remoteAddr, file);

    // Datei schlieen und Winsock bereinigen
    fclose(file);
    closesocket(sock);
    WSACleanup();
    return 0;
}
