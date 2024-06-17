#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define PORT 8889
#define BUFFER_SIZE 1024

void *server(void *arg) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t thread_id;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Error al crear el socket del servidor");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error al enlazar el socket del servidor");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 5) < 0) {
        perror("Error al escuchar en el socket del servidor");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Servidor escuchando en el puerto %d...\n", PORT);

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            perror("Error al aceptar la conexión del cliente");
            continue;
        }

        printf("Cliente conectado\n");

        if (pthread_create(&thread_id, NULL, conexion, (void *)&client_socket) != 0) {
            perror("Error al crear el hilo para el cliente");
            close(client_socket);
            close(server_socket);
            return NULL;
        }
    }
}



void *client(void *arg) {
    int client_socket = -1;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char command[BUFFER_SIZE];

    while (1) {
        printf("ftp> ");
        fgets(command, BUFFER_SIZE, stdin);
        command[strcspn(command, "\n")] = 0; // Eliminar el salto de línea

        // Parsear el comando
        char *cmd = strtok(command, " ");
        char *arg = strtok(NULL, "");

        if (strcmp(cmd, "open") == 0) {
            if (arg == NULL) {
                printf("Uso: open <IP>\n");
                continue;
            }

            client_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (client_socket < 0) {
                perror("Error al crear el socket del cliente");
                continue;
            }

            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(PORT);
            if (inet_pton(AF_INET, arg, &server_addr.sin_addr) <= 0) {
                perror("Dirección IP inválida");
                close(client_socket);
                client_socket = -1;
                continue;
            }

            if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                perror("Error al conectar al servidor");
                close(client_socket);
                client_socket = -1;
                continue;
            }

            printf("Conectado al servidor %s\n", arg);
        } else if (strcmp(cmd, "close") == 0) {
            if (client_socket != -1) {
                close(client_socket);
                client_socket = -1;
                printf("Conexión cerrada\n");
            } else {
                printf("No hay conexión activa\n");
            }
        } else if (strcmp(cmd, "quit") == 0) {
            if (client_socket != -1) {
                close(client_socket);
            }
            printf("Saliendo...\n");
            exit(0);
        } else if (strcmp(cmd, "lcd") == 0) {
            if (arg == NULL) {
                printf("Uso: lcd <directorio>\n");
                continue;
            }

            if (chdir(arg) == 0) {
                printf("Directorio actual del cliente cambiado a %s\n", arg);
                if (getcwd(client_current_dir, sizeof(client_current_dir)) == NULL) {
                    perror("Error al obtener el directorio actual del cliente");
                }
            } else {
                perror("Error al cambiar el directorio actual del cliente");
            }
        } else if (client_socket == -1) {
            printf("Debe abrir una conexión primero usando el comando 'open <IP>'\n");
        } else {
            if (strcmp(cmd, "put") == 0) {
                char file_path[BUFFER_SIZE];
                snprintf(file_path, BUFFER_SIZE, "%s/%s", client_current_dir, arg);
                FILE *file = fopen(file_path, "rb");
                if (file == NULL) {
                    perror("Error al abrir el archivo local");
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
                        perror("Error al crear el archivo local");
                        continue;
                    }

                    fwrite(buffer, 1, bytes_received, file);
                    fclose(file);
                    printf("Archivo %s recibido\n", arg);
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



void *conexion(void *client_socket) {
    int socket = *(int *)client_socket;
    char buffer[BUFFER_SIZE];
    int bytes_read;
    char server_current_dir[BUFFER_SIZE];

    if (getcwd(server_current_dir, sizeof(server_current_dir)) == NULL) {
        perror("Error al obtener el directorio actual del servidor");
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
                snprintf(response, BUFFER_SIZE, "550 No directory specified\n");
            } else {
                char new_dir[BUFFER_SIZE];
                snprintf(new_dir, BUFFER_SIZE, "%s/%s", server_current_dir, arg);
                if (chdir(new_dir) == 0) {
                    snprintf(response, BUFFER_SIZE, "250 Directory changed to %s\n", arg);
                    strcpy(server_current_dir, new_dir);
                } else {
                    snprintf(response, BUFFER_SIZE, "550 Failed to change directory\n");
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
                snprintf(response, BUFFER_SIZE, "550 Failed to list directory\n");
            }
        } else if (strcmp(cmd, "pwd") == 0) {
            snprintf(response, BUFFER_SIZE, "%s\n", server_current_dir);
        } else if (strcmp(cmd, "get") == 0) {
            if (arg == NULL) {
                snprintf(response, BUFFER_SIZE, "550 No file specified\n");
            } else {
                char file_path[BUFFER_SIZE];
                snprintf(file_path, BUFFER_SIZE, "%s/%s", server_current_dir, arg);
                FILE *file = fopen(file_path, "rb");
                if (file == NULL) {
                    snprintf(response, BUFFER_SIZE, "550 File not found\n");
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
                snprintf(response, BUFFER_SIZE, "550 No file specified or content missing\n");
            } else {
                char file_path[BUFFER_SIZE];
                snprintf(file_path, BUFFER_SIZE, "%s/%s", server_current_dir, arg);
                FILE *file = fopen(file_path, "wb");
                if (file == NULL) {
                    snprintf(response, BUFFER_SIZE, "550 Failed to create file\n");
                } else {
                    fwrite(file_content, 1, strlen(file_content), file);
                    fclose(file);
                    snprintf(response, BUFFER_SIZE, "226 Transfer complete\n");
                }
            }
        } else {
            snprintf(response, BUFFER_SIZE, "502 Command not implemented\n");
        }

        send(socket, response, strlen(response), 0);
    }

    close(socket);
    printf("Cliente desconectado\n");
    return NULL;
}



int main() {
    pthread_t server_tid, client_tid;

    if (getcwd(client_current_dir, sizeof(client_current_dir)) == NULL) {
        perror("Error al obtener el directorio actual del cliente");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&server_tid, NULL, server, NULL) != 0) {
        perror("Error al crear el hilo del servidor");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&client_tid, NULL, client, NULL) != 0) {
        perror("Error al crear el hilo del cliente");
        exit(EXIT_FAILURE);
    }

    pthread_join(server_tid, NULL);
    pthread_join(client_tid, NULL);

    return 0;
}