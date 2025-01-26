#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define BUFFER_SIZE 512         // Maximale Größe der Daten pro Paket
#define TIMEOUT_INTERVAL 300    // Timeout fr select (300 ms)

// Struktur fr eine Anfrage
struct request {
    unsigned char ReqType;      // Anfragetyp (z.B. Hello, Close, Daten)
    unsigned long SeNr;         // Sequenznummer
    char data[BUFFER_SIZE];     // Die Daten der Datei, die bertragen werden
};

// Funktion zum Empfangen einer Datei
void recv_file(SOCKET sock) {
    struct sockaddr_in6 remoteAddr;                 // Struktur zur Speicherung der Empfängeradresse
    int remoteAddrSize = sizeof(remoteAddr);        // Variable zur Speicherung der Größe der Empfängeradresse
    struct request req;                             // Struktur zum Speichern der Anfrage
    FILE* file;
    fopen_s(&file, "received_file.txt", "wb");      // Öffne eine Datei zum Schreiben der empfangenen Daten, write binary

    unsigned long expectedSeqNr = 1;                // erwartete Sequenznummer für die nächsten Datenpakete
    fd_set read_fds;                                // Set zum Überprüfen der zu lesenden Sockets
    struct timeval timeout;                         // Timeout für select()

    while (1) {
        FD_ZERO(&read_fds);                         // Bereinige fd_set (keine Socjets in Sammlung)
        FD_SET(sock, &read_fds);                    // Hinzufügen Socket zum Set der Dateideskriptoren
        timeout.tv_sec = 0;                         // Sekunden auf 0
        timeout.tv_usec = TIMEOUT_INTERVAL * 1000;  // Timer in Mikrosekunden

        // ist der socket bereit innerhalb der timeout-periode (300ms) Daten zu empfangen?
        if (select(sock + 1, &read_fds, NULL, NULL, &timeout) > 0) {
            recvfrom(sock, (char*)&req, sizeof(req), 0, (struct sockaddr*)&remoteAddr, &remoteAddrSize);    // Daten empfangen
            req.SeNr = ntohl(req.SeNr);

            // Überprüfe Nachrichtentyp
            if (req.ReqType == 'H') {  // Hello empfangen
                printf("Hello-Nachricht empfangen! Sende ACK...\n");

                // ACK senden
                struct request ack;
                ack.ReqType = 'A';      // 'A' für ACK
                ack.SeNr = htonl(0);    // Sequenznummer auf 0 für Hello
                sendto(sock, (const char*)&ack, sizeof(ack), 0, (struct sockaddr*)&remoteAddr, remoteAddrSize);
                continue;               
            }

            // Überprüfe, ob es sich um ein Datenpaket handelt
            if (req.ReqType == 'D') {  // Datenpaket empfangen
                printf("Empfangen: SeqNr = %lu, Erwartet: %lu, Daten = %s\n", req.SeNr, expectedSeqNr, req.data);

                if (req.SeNr == expectedSeqNr) {                            // entspricht erwarteter Sequenznummer
                    fwrite(req.data, sizeof(char), strlen(req.data), file); // Zeile schreiben
                    expectedSeqNr++;                                        // erwartete Sequenznummer erhöhen

                    // ACK senden
                    struct request ack;
                    ack.ReqType = 'A';                  // 'A' für ACK
                    ack.SeNr = htonl(req.SeNr);         // aktuelle Sequenznummer senden
                    sendto(sock, (const char*)&ack, sizeof(ack), 0, (struct sockaddr*)&remoteAddr, remoteAddrSize);
                }
                else {
                    // NACK senden
                    struct request nack;
                    nack.ReqType = 'N';                 // 'N' für NACK
                    nack.SeNr = htonl(expectedSeqNr);   // erwartete Sequenznummer senden
                    sendto(sock, (const char*)&nack, sizeof(nack), 0, (struct sockaddr*)&remoteAddr, remoteAddrSize);
                }
            }

            // Überprüfe, ob Close-Nachricht empfangen wurde
            if (req.ReqType == 'C') {       // 'C' fr Close
                printf("Empfangen: Close-Nachricht. Sende Close-ACK...\n");
                // Sende ACK für Close-Nachricht
                struct request closeAck;
                closeAck.ReqType = 'A';     // 'A' für ACK
                closeAck.SeNr = htonl(0);   // Sequenznummer auf 0 für Close
                sendto(sock, (const char*)&closeAck, sizeof(closeAck), 0, (struct sockaddr*)&remoteAddr, remoteAddrSize);
                break;
            }
        }
    }

    fclose(file);   // Datei schließen
}

int main(int argc, char* argv[]) {
    if (argc < 3) { // Überprüfe, ob genügend Argumente übergeben wurden
        fprintf(stderr, "Usage: %s <Multicast-Adresse> <Port>\n", argv[0]);
        return 1;
    }

    const char* multicast_addr = argv[1];   // Multicast-Adresse
    int port = atoi(argv[2]);               // Port, auf dem Server lauscht

    // Initialisieren von Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        // Fehler bei der Initialisierung von Winsock
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    // Erstellen eines Sockets für UDPv6
    SOCKET sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        // Fehler beim Erstellen des Sockets
        fprintf(stderr, "Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    struct sockaddr_in6 localAddr = { 0 };  // Struktur für lokale Adresse
    localAddr.sin6_family = AF_INET6;       // Adressfamilie AF_INET6 (IPv6)
    localAddr.sin6_port = htons(port);      // Umwandlung Portnummer in Netzwerkformat
    localAddr.sin6_addr = in6addr_any;      // Lauscht auf alle verfgbaren Interfaces

    // Binde das Socket an die lokale Adresse
    bind(sock, (struct sockaddr*)&localAddr, sizeof(localAddr));

    struct ipv6_mreq multicastRequest;
    inet_pton(AF_INET6, multicast_addr, &multicastRequest.ipv6mr_multiaddr);    // Konvertiere Multicast-IP-Adresse
    multicastRequest.ipv6mr_interface = 0;                                      // Setze das Interface für Multicast

    // Trete der Multicast-Gruppe bei
    setsockopt(sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (char*)&multicastRequest, sizeof(multicastRequest));

    printf("Server läuft und wartet auf Daten...\n");

    // Funktion zum Empfangen der Datei auf
    recv_file(sock);

    // Socket schlieen und Winsock bereinigen
    closesocket(sock);
    WSACleanup();
    return 0;
}
