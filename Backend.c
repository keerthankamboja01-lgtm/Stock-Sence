#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080
#define BUFFER_SIZE 8192
#define MAX_ITEMS 100

typedef struct {
    char name[64];
    char sku[32];
    char category[32];
    char location[32];
    int quantity;
} Product;

Product inventory[MAX_ITEMS];
int product_count = 0;

void init_data() {
    strcpy(inventory[0].name, "Steel Rod 10mm");
    strcpy(inventory[0].sku, "STL-100");
    strcpy(inventory[0].category, "Raw Material");
    strcpy(inventory[0].location, "Main Store");
    inventory[0].quantity = 100;

    strcpy(inventory[1].name, "Wooden Chair");
    strcpy(inventory[1].sku, "CHR-200");
    strcpy(inventory[1].category, "Furniture");
    strcpy(inventory[1].location, "Rack B");
    inventory[1].quantity = 25;

    product_count = 2;
}

void send_response(int client_fd, const char *status, const char *content_type, const char *body) {
    char header[1024];
    int body_len = strlen(body);
    snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n",
        status, content_type, body_len);

    send(client_fd, header, strlen(header), 0);
    send(client_fd, body, body_len, 0);
}

void extract_json_val(const char *json, const char *key, char *out_val) {
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);
    char *start = strstr(json, search_key);
    if (!start) { out_val[0] = '\0'; return; }
    
    start += strlen(search_key);
    while (*start == ' ' || *start == '"') start++;
    
    int i = 0;
    while (*start != '"' && *start != ',' && *start != '}' && *start != '\0') {
        out_val[i++] = *start++;
    }
    out_val[i] = '\0';
}

void handle_dashboard(int client_fd) {
    char json_body[4096];
    int pos = snprintf(json_body, sizeof(json_body), "{\"inventory\":[");

    for (int i = 0; i < product_count; i++) {
        pos += snprintf(json_body + pos, sizeof(json_body) - pos,
            "{\"name\":\"%s\",\"sku\":\"%s\",\"category\":\"%s\",\"location\":\"%s\",\"quantity\":%d}%s",
            inventory[i].name, inventory[i].sku, inventory[i].category,
            inventory[i].location, inventory[i].quantity,
            (i < product_count - 1) ? "," : "");
    }
    snprintf(json_body + pos, sizeof(json_body) - pos, "]}");

    send_response(client_fd, "200 OK", "application/json", json_body);
}

void handle_add_product(int client_fd, const char *body) {
    if (product_count < MAX_ITEMS) {
        Product p;
        extract_json_val(body, "name", p.name);
        extract_json_val(body, "sku", p.sku);
        extract_json_val(body, "category", p.category);
        strcpy(p.location, "Main Store");
        p.quantity = 0;

        inventory[product_count++] = p;
        send_response(client_fd, "200 OK", "application/json", "{\"status\":\"success\"}");
    } else {
        send_response(client_fd, "400 Bad Request", "application/json", "{\"status\":\"full\"}");
    }
}

void handle_stock_update(int client_fd, const char *body) {
    char sku[32], qty_str[16];
    extract_json_val(body, "sku", sku);
    extract_json_val(body, "quantity", qty_str);

    int new_qty = atoi(qty_str);
    int found = 0;

    for (int i = 0; i < product_count; i++) {
        if (strcmp(inventory[i].sku, sku) == 0) {
            inventory[i].quantity = new_qty;
            found = 1;
            break;
        }
    }

    if (found) {
        send_response(client_fd, "200 OK", "application/json", "{\"status\":\"updated\"}");
    } else {
        send_response(client_fd, "404 Not Found", "application/json", "{\"status\":\"sku_not_found\"}");
    }
}

void handle_operation(int client_fd, const char *body) {
    char type[32], sku[32], from[32], to[32], qty_str[16];
    extract_json_val(body, "type", type);
    extract_json_val(body, "sku", sku);
    extract_json_val(body, "from", from);
    extract_json_val(body, "to", to);
    extract_json_val(body, "qty", qty_str);

    int qty = atoi(qty_str);

    for (int i = 0; i < product_count; i++) {
        if (strcmp(inventory[i].sku, sku) == 0) {
            if (strcmp(type, "RECEIPT") == 0) {
                inventory[i].quantity += qty;
                strcpy(inventory[i].location, to);
            } else if (strcmp(type, "DELIVERY") == 0) {
                inventory[i].quantity -= qty;
                if (inventory[i].quantity < 0) inventory[i].quantity = 0;
            } else if (strcmp(type, "TRANSFER") == 0) {
                strcpy(inventory[i].location, to);
            }
            break;
        }
    }

    send_response(client_fd, "200 OK", "application/json", "{\"status\":\"updated\"}");
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];

    init_data();

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("StockSense C Backend running on http://localhost:%d\n", PORT);

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_fd < 0) continue;

        memset(buffer, 0, BUFFER_SIZE);
        read(client_fd, buffer, BUFFER_SIZE - 1);

        if (strncmp(buffer, "OPTIONS", 7) == 0) {
            send_response(client_fd, "200 OK", "text/plain", "");
        } 
        else if (strstr(buffer, "GET /api/dashboard")) {
            handle_dashboard(client_fd);
        } 
        else if (strstr(buffer, "POST /api/products")) {
            char *body = strstr(buffer, "\r\n\r\n");
            if (body) body += 4;
            handle_add_product(client_fd, body ? body : "");
        } 
        else if (strstr(buffer, "POST /api/stock/update")) {
            char *body = strstr(buffer, "\r\n\r\n");
            if (body) body += 4;
            handle_stock_update(client_fd, body ? body : "");
        }
        else if (strstr(buffer, "POST /api/operations")) {
            char *body = strstr(buffer, "\r\n\r\n");
            if (body) body += 4;
            handle_operation(client_fd, body ? body : "");
        } 
        else {
            send_response(client_fd, "404 Not Found", "text/plain", "Not Found");
        }

        close(client_fd);
    }

    return 0;
}
