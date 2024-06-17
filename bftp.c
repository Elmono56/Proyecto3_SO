#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#define PORT 8889
#define BUFFER_SIZE 1024

void *handle_client(void *client_socket);
void *server_thread(void *arg);
void *client_thread(void *arg);

char client_current_dir[BUFFER_SIZE];

int main() {
    pthread_t server_tid, client_tid;

    if (getcwd(client_current_dir, sizeof(client_current_dir)) == NULL) {
        perror("Error: no se pudo obtener el directorio del cliente");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&server_tid, NULL, server_thread, NULL) != 0) {
        perror("Error: no se pudo crear el hilo del servidor");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&client_tid, NULL, client_thread, NULL) != 0) {
        perror("Error: no se pudo crear el hilo del cliente");
        exit(EXIT_FAILURE);
    }

    pthread_join(server_tid, NULL);
    pthread_join(client_tid, NULL);

    return 0;
}

void *server_thread(void *arg) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t thread_id;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Error: no se pudo crear el socket del servidor");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error: no se pudo enlazar el socket del servidor");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 5) < 0) {
        perror("Error: no se pudo abrir el puerto del servidor");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Servidor abierto en el puerto %d\n", PORT);

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            perror("Error: no se pudo aceptar el enlace con el cliente");
            continue;
        }

        printf("Conexión satisfactoria con el cliente\n");

        if (pthread_create(&thread_id, NULL, handle_client, (void *)&client_socket) != 0) {
            perror("Error: no se pudo crear el hilo del cliente");
            close(client_socket);
            close(server_socket);
            return NULL;
        }
    }
}

void *handle_client(void *client_socket) {
    int socket = *(int *)client_socket;
    char buffer[BUFFER_SIZE];
    int bytes_read;
    char server_current_dir[BUFFER_SIZE];

    if (getcwd(server_current_dir, sizeof(server_current_dir)) == NULL) {
        perror("Error: no se pudo obtener el directorio del servidor");
        close(socket);
        return NULL;
    }

    while ((bytes_read = recv(socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_read] = '\0';
        printf("Comando recibido: %s\n", buffer);

        char *cmd = strtok(buffer, " ");
        char *arg = strtok(NULL, " ");
        char *file_content = strtok(NULL, "");
        char response[BUFFER_SIZE];
        memset(response, 0, BUFFER_SIZE);

        if (strcmp(cmd, "cd") == 0) {
            if (arg == NULL) {
                snprintf(response, BUFFER_SIZE, "Error: no se ha encontrado el directorio solicitado\n");
            } else {
                char new_dir[BUFFER_SIZE];
                snprintf(new_dir, BUFFER_SIZE, "%s/%s", server_current_dir, arg);
                if (chdir(new_dir) == 0) {
                    snprintf(response, BUFFER_SIZE, "El directorio se ha cambiado por: %s\n", arg);
                    strcpy(server_current_dir, new_dir);
                } else {
                    snprintf(response, BUFFER_SIZE, "Error: no se pudo cambiar el directorio\n");
                }
            }
        } else if (strcmp(cmd, "ls") == 0) {
            DIR *d;
            struct dirent *dir;
            d = opendir(server_current_dir);
            if (d) {
                response[0] = '\0';
                while ((dir = readdir(d)) != NULL) {
                    strcat(response, dir->d_name);
                    strcat(response, "\n");
                }
                closedir(d);
            } else {
                snprintf(response, BUFFER_SIZE, "Error: no se pudo listar el directorio\n");
            }
        } else if (strcmp(cmd, "pwd") == 0) {
            snprintf(response, BUFFER_SIZE, "%s\n", server_current_dir);
        } else if (strcmp(cmd, "get") == 0) {
            if (arg == NULL) {
                snprintf(response, BUFFER_SIZE, "Error: no se especificó el archivo \n");
            } else {
                char file_path[BUFFER_SIZE];
                snprintf(file_path, BUFFER_SIZE, "%s/%s", server_current_dir, arg);
                FILE *file = fopen(file_path, "rb");
                if (file == NULL) {
                    snprintf(response, BUFFER_SIZE, "Error: el archivo solicitado no existe \n");
                } else {
                    fseek(file, 0, SEEK_END);
                    long file_size = ftell(file);
                    fseek(file, 0, SEEK_SET);
                    char *file_content = malloc(file_size);
                    fread(file_content, 1, file_size, file);
                    fclose(file);
                    send(socket, file_content, file_size, 0);
                    free(file_content);
                    continue;
                }
            }
        } else if (strcmp(cmd, "put") == 0) {
            if (arg == NULL || file_content == NULL) {
                snprintf(response, BUFFER_SIZE, "Error: no se solicitó ningún archivo \n");
            } else {
                char file_path[BUFFER_SIZE];
                snprintf(file_path, BUFFER_SIZE, "%s/%s", server_current_dir, arg);
                FILE *file = fopen(file_path, "wb");
                if (file == NULL) {
                    snprintf(response, BUFFER_SIZE, "Error: no se pudo crear el archivo \n");
                } else {
                    fwrite(file_content, 1, strlen(file_content), file);
                    fclose(file);
                    snprintf(response, BUFFER_SIZE, "La transferencia fue exitosa\n");
                }
            }
        } else {
            snprintf(response, BUFFER_SIZE, "Error: el comando solicitado no existe \n");
        }

        send(socket, response, strlen(response), 0);
    }

    close(socket);
    printf("El cliente se ha desconectado\n");
    return NULL;
}

void *client_thread(void *arg) {
    int client_socket = -1;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char command[BUFFER_SIZE];

    while (1) {
        printf("bftp> ");
        fgets(command, BUFFER_SIZE, stdin);
        command[strcspn(command, "\n")] = 0;

        char *cmd = strtok(command, " ");
        char *arg = strtok(NULL, "");

        if (strcmp(cmd, "open") == 0) {
            if (arg == NULL) {
                printf("Uso: open <dirección IP> \n");
                continue;
            }

            client_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (client_socket < 0) {
                perror("Error: no se pudo crear el socket del cliente");
                continue;
            }

            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(PORT);
            if (inet_pton(AF_INET, arg, &server_addr.sin_addr) <= 0) {
                perror("Error: la dirección IP ingresada es inválida");
                close(client_socket);
                client_socket = -1;
                continue;
            }

            if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                perror("Error: no se pudo establecer conexión con el servidor");
                close(client_socket);
                client_socket = -1;
                continue;
            }

            printf("Cliente conectándose al servidor  %s\n", arg);
        } else if (strcmp(cmd, "close") == 0) {
            if (client_socket != -1) {
                close(client_socket);
                client_socket = -1;
                printf("Se ha finalizado la conexión con el servidor \n");
            } else {
                printf("Error: no se ha encontrado ninguna conexión abierta \n");
            }
        } else if (strcmp(cmd, "quit") == 0) {
            if (client_socket != -1) {
                close(client_socket);
            }
            printf("Saliendo del programa \n");
            exit(0);
        } else if (strcmp(cmd, "lcd") == 0) {
            if (arg == NULL) {
                printf("Uso: lcd <directorio>\n");
                continue;
            }

            if (chdir(arg) == 0) {
                printf("Se ha cambiado la dirección del cliente a %s\n", arg);
                if (getcwd(client_current_dir, sizeof(client_current_dir)) == NULL) {
                    perror("Error: no se pudo obtener el directorio actual del cliente");
                }
            } else {
                perror("Error: no se pudo cambiar el directorio actual del cliente");
            }
        } else if (client_socket == -1) {
            printf("Error: no se ha establecido ninguna conexion'\n");
        } else {
            if (strcmp(cmd, "put") == 0) {
                char file_path[BUFFER_SIZE];
                snprintf(file_path, BUFFER_SIZE, "%s/%s", client_current_dir, arg);
                FILE *file = fopen(file_path, "rb");
                if (file == NULL) {
                    perror("Error: no se pudo abrir el archivo local");
                    continue;
                }

                fseek(file, 0, SEEK_END);
                long file_size = ftell(file);
                fseek(file, 0, SEEK_SET);
                char *file_content = malloc(file_size);
                fread(file_content, 1, file_size, file);
                fclose(file);

                snprintf(buffer, BUFFER_SIZE, "put %s %s", arg, file_content);
                send(client_socket, buffer, strlen(buffer), 0);
                free(file_content);

                int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    printf("%s\n", buffer);
                }
            } else if (strcmp(cmd, "get") == 0) {
                snprintf(buffer, BUFFER_SIZE, "get %s", arg);
                send(client_socket, buffer, strlen(buffer), 0);

                int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    printf("Recibiendo archivo %s\n", arg);

                    char file_path[BUFFER_SIZE];
                    snprintf(file_path, BUFFER_SIZE, "%s/%s", client_current_dir, arg);
                    FILE *file = fopen(file_path, "wb");
                    if (file == NULL) {
                        perror("Error: no se pudo crear el archivo de manera local");
                        continue;
                    }

                    fwrite(buffer, 1, bytes_received, file);
                    fclose(file);
                    printf("El archivo %s ha sido recibido exitosamente \n", arg);
                }
            } else {
                snprintf(buffer, BUFFER_SIZE, "%s %s", cmd, arg ? arg : "");
                send(client_socket, buffer, strlen(buffer), 0);

                int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    printf("%s\n", buffer);
                }
            }
        }
    }
}
