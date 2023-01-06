// TransportServer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <winsock2.h>
#include <string>

#define BUF_SIZE			(8 << 10)
#define CMD_GET_FILE		0
#define CMD_SEND_FILE		1
#define CMD_IS_EXIT_FILE	100
#define CMD_CHK_DIR			101
#define CMD_DATA_SIZE		10
#define CMD_DATA			11

char g_Password[] = { 'P', 'A', 'S', 'S', 'W', 'O', 'D' };

using std::string;

#pragma comment(lib, "ws2_32.lib")


int SetKeepAlive(SOCKET sock,int nOn)
{
	struct tcp_keepalive {
		u_long  onoff;
		u_long  keepalivetime;
		u_long  keepaliveinterval;
	};
	DWORD dwError = 0L ,dwBytes=0;
	struct tcp_keepalive sKA_Settings = {0}, sReturned = {0} ;

	sKA_Settings.onoff = nOn ;
	sKA_Settings.keepalivetime = 6000 ;
	sKA_Settings.keepaliveinterval = 6000 ;
	if (WSAIoctl(sock,/*SIO_KEEPALIVE_VALS*/ 0x98000004, &sKA_Settings,
		sizeof(sKA_Settings), &sReturned, sizeof(sReturned), &dwBytes,
		NULL, NULL) != 0)
	{
		dwError = WSAGetLastError() ;
	}

	return 1;
}

struct SCommand
{
	int nCommand;
	char cPathUNC[MAX_PATH];
};

int FileExists(TCHAR * file)
{
	WIN32_FIND_DATA FindFileData;
	HANDLE handle = FindFirstFile(file, &FindFileData);
	int found = handle != INVALID_HANDLE_VALUE;
	if (found)
	{
		//FindClose(&handle); this will crash
		FindClose(handle);
	}
	return found;
}


int CheckDirectory(char* strFullPath) {
	string path(strFullPath);
	string directory;
	const size_t last_slash_idx = path.rfind('\\');
	if (std::string::npos != last_slash_idx)
	{
		directory = path.substr(0, last_slash_idx);
	}
	if (directory.empty()) 
	{
		return 0;
	}
	if (CreateDirectory(directory.c_str(), NULL) || ERROR_ALREADY_EXISTS == GetLastError())
	{
		return 1;
	}
	return 0;
}


int MakeDirectory(std::string path)
{
	string directory;
	const size_t last_slash_idx = path.rfind('\\');
	if (std::string::npos != last_slash_idx)
	{
		directory = path.substr(0, last_slash_idx);
	}
	if (directory.empty()) {
		return 0;
	}

	signed int pos = 0;
	do
	{
		pos = directory.find_first_of("\\/", pos + 1);
		if (GetFileAttributes(directory.substr(0, pos).c_str()) == INVALID_FILE_ATTRIBUTES)
			CreateDirectory(directory.substr(0, pos).c_str(), NULL);
	} while (pos != std::wstring::npos);

	DWORD dwAttrib = GetFileAttributes(directory.c_str());

	if (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
		return 1;
	else
		return 0;
}

void DecryptData(char* data, int size, DWORD &offset) {
	for (int i = 0; i < size; i++) {
		int nPasswordLength = sizeof(g_Password);
		// data[i] = data[i] ^ g_Password[i % nPasswordLength];
		data[i] = data[i] ^ g_Password[offset];
		offset++;
		if (offset >= nPasswordLength) offset = 0;
	}
}

DWORD WINAPI ProcessThread(LPVOID lpParam) 
{
	SOCKET ConnectSocket = (SOCKET)lpParam;

	int bytesRecv = SOCKET_ERROR;
	SCommand sCommand;
	sCommand.nCommand = -1;

	while( TRUE)
	{
		bytesRecv = recv( ConnectSocket,(char*)&sCommand, sizeof(SCommand), 0 );
		if ( bytesRecv == SOCKET_ERROR || bytesRecv == 0 || bytesRecv == WSAECONNRESET )
		{
			printf( "Connection Closed.\n");
			break;
		}
		else
		{
			DWORD dwSize = 0;
			if( sCommand.nCommand == CMD_GET_FILE)
			{
				HANDLE hFile = CreateFile(sCommand.cPathUNC,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);  // no attr. template
				if (hFile == INVALID_HANDLE_VALUE) 
				{
					send(ConnectSocket,(char*)&dwSize,sizeof(dwSize),0);
					continue;
				}

				dwSize = GetFileSize(hFile,NULL);
				if(dwSize == INVALID_FILE_SIZE)
				{
					send(ConnectSocket,(char*)&dwSize,sizeof(dwSize),0);
					CloseHandle(hFile);
					continue;
				}

				send(ConnectSocket,(char*)&dwSize,sizeof(dwSize),0);
				char cRead[512];
				do
				{
					DWORD nBytesRead = 0;
					BOOL bResult = ReadFile(hFile,cRead,512,&nBytesRead,NULL); 
					// Check for end of file. 
					if (bResult &&  nBytesRead == 0) 
					{
						break;
					} 
					send(ConnectSocket,cRead,nBytesRead,0);
				}
				while(1);

				CloseHandle(hFile);
			}
			else if (sCommand.nCommand == CMD_IS_EXIT_FILE)
			{

				printf("-100\n");
				char bufResponse[2];
				if (FileExists(sCommand.cPathUNC)) {
					bufResponse[0] = 'Y';
					printf("y");
				}
				else {
					bufResponse[0] = 'N';
					printf("n");
				}
				dwSize = 2;
				send(ConnectSocket, (char*)&dwSize, sizeof(dwSize), 0);
				int ret = send(ConnectSocket, bufResponse, 2, 0);

				printf("send--%d", ret);
			}
			else if (sCommand.nCommand == CMD_SEND_FILE)
			{
				if (MakeDirectory(sCommand.cPathUNC) == 0)
				{
					break;
				}
				DWORD dwFileCreated = 0;
				HANDLE hFile = CreateFile(sCommand.cPathUNC, GENERIC_WRITE,	FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);  // no attr. template
				if (hFile == INVALID_HANDLE_VALUE)
				{
					send(ConnectSocket, (char*)&dwFileCreated, sizeof(dwFileCreated), 0);
					continue;
				}
				dwFileCreated = 1;
				send(ConnectSocket, (char*)&dwFileCreated, sizeof(dwFileCreated), 0);
				DWORD nFileSize = 0;
				DWORD nBytesRecv = 0;
				recv(ConnectSocket, (char*)&nFileSize, sizeof(DWORD), 0);
				DWORD dwWait = 0;
				DWORD offset = 0;
				while (nFileSize > nBytesRecv)
				{
					char szBuffer[BUF_SIZE];
					DWORD toRead = BUF_SIZE;
					if (nFileSize - nBytesRecv < toRead) toRead = nFileSize - nBytesRecv;
					DWORD nBytesRead = recv(ConnectSocket, szBuffer, toRead, 0);
					// printf("nbytesread = %d\n", nBytesRead);
					if (nBytesRead == 0)
					{
						//send(ConnectSocket, (char*)&dwWait, sizeof(dwWait), 0);
						break;
					}
					DecryptData(szBuffer, nBytesRead, offset);
					DWORD nBytesWritten = 0;
					BOOL bResult = WriteFile(hFile, szBuffer, nBytesRead, &nBytesWritten, NULL);
					if (bResult && nBytesWritten == 0)
					{
						//send(ConnectSocket, (char*)&dwWait, sizeof(dwWait), 0);
						break;
					}
					//send(ConnectSocket, (char*)&dwWait, sizeof(dwWait), 0);
					nBytesRecv += nBytesRead;
				}
				send(ConnectSocket, (char*)&nBytesRecv, sizeof(nBytesRecv), 0);
				CloseHandle(hFile);
			}
			else if (sCommand.nCommand == CMD_CHK_DIR)//Check output directory.
			{
				char bufResponse[2];
				if (CheckDirectory(sCommand.cPathUNC))
				{
					bufResponse[0] = 'Y';
				}
				else 
				{
					bufResponse[0] = 'N';
				}
				dwSize = 2;
				send(ConnectSocket, (char*)&dwSize, sizeof(dwSize), 0);
				send(ConnectSocket, bufResponse, 2, 0);
			}
		}
	}

	closesocket(ConnectSocket);
	return 0;
}

void ProcessSocket(SOCKET ConnectSocket)
{
	DWORD dwThreadId = 0; 
	HANDLE hThread = CreateThread(NULL,0,ProcessThread,(LPVOID)ConnectSocket,0,&dwThreadId);
	CloseHandle( hThread);
}

int main( int argc, char **argv )
{
	int nHide = 0,nPort = 0;
	int i = 0;
	for(i = 1;i < argc;i++)
	{
		if( _stricmp(argv[i],"-hide")==0 )
		{
			nHide = 1;
		}
		else
		{
			break;
		}
	}
	nPort = i;

	if( argc - nPort < 1 )
	{
		goto usage;
	}

	WORD wVersionRequested;
	WSADATA wsaData;
	wVersionRequested = MAKEWORD( 2, 2 );
	if(WSAStartup( wVersionRequested, &wsaData ))
	{
		printf("WSAStartup failed");
		exit(1);
	}

	if(LOBYTE(wsaData.wVersion) < 2)
	{
		printf("Version 2.x or higher of WinSock is needed but was not found");
		exit(1);
	}

	struct sockaddr_in sin;
	memset(&sin, 0,sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons((u_short)atoi(argv[nPort]));
	if(sin.sin_port == 0)
	{
		struct servent* pse = getservbyname(argv[nPort],"tcp");
		if(pse)
		{
			sin.sin_port = pse->s_port;
		}
		else
		{
			printf("Port or service %s is not valid.\n",argv[nPort]);
			exit(1);
		}
	}

	struct protoent* ppe = getprotobyname("tcp");
	SOCKET sckListener = socket(PF_INET, SOCK_STREAM, ppe->p_proto);
	if( sckListener == INVALID_SOCKET )
	{
		printf("Could not create listening socket on port or service %s\n",argv[nPort]);
		printf("Error code from WSAGetLastError()=%d\n",WSAGetLastError());
		printf("Perhaps you need to install WinSock version 2.\n");
		printf("You can get WS2SETUP.EXE from Microsoft web pages,\n");
		printf("which is an install program for WinSock version 2.\n");
		exit(1);
	}

	int opt = 1;
	if( setsockopt(sckListener, SOL_SOCKET, SO_REUSEADDR, (const char FAR*)&opt, sizeof(opt)) )
	{
		printf("Could not set reuse socket option on socket for port or service %s\n",argv[nPort]);
		printf("Error code from WSAGetLastError()=%d\n",WSAGetLastError());
		exit(1);
	}

	if( bind(sckListener, (struct sockaddr *)&sin, sizeof(sin)) )
	{
		printf("Could not bind to port or service %s\n",argv[nPort]);
		printf("Error code from WSAGetLastError()=%d\n",WSAGetLastError());
		exit(1);
	}

	if( listen(sckListener,SOMAXCONN) )
	{
		printf("Could not listen on port or service %s\n",argv[nPort]);
		printf("Error code from WSAGetLastError()=%d\n",WSAGetLastError());
		WSACleanup();
		exit(1);
	}

	if(nHide)
	{
		FreeConsole();
	}

	while(TRUE)
	{
		struct sockaddr_in fsin;
		int fsinlen = sizeof(fsin);
		SOCKET sckConnected = accept(sckListener,(struct sockaddr *)&fsin,&fsinlen);
		if( sckConnected == INVALID_SOCKET )
		{
			if( nHide )
			{
				AllocConsole();
			}

			printf("Accept failed on port or service %s\n",argv[1]);
			printf("Error code from WSAGetLastError()=%d\n",WSAGetLastError());

			if( nHide )
			{
				getchar();
			}
			exit(1);
		}

		SetKeepAlive(sckConnected,TRUE);
		ProcessSocket(sckConnected);
	}

usage:
	if(nHide)
	{
		AllocConsole();
	}

	printf("Usage: TransportServer [-hide] port ...");

	if(nHide)
	{
		getchar();
	}

	return 0;
}
