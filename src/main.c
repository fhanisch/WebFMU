/// @date: 01.12.2017
/// @author: fh
/// @url: https://github.com/fhanisch/WebFMU.git

/*
	compile: gcc -std=c11 -Wall -o webfmuserver src/main.c -L . -ldl -lmatio -pthread
	Wichtig: Cache-Control im Header beachten !!! --> Wann müssen welche Seiten aktualisiert werden?

	ToDo:
		- Windows Portierung
		- Log Handling
        - _WINSOCK_DEPRECATED_NO_WARNINGS entfernen
*/

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
//#define WINDOWS  // --> wird über Compiler Optionen definiert
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fmi2FunctionTypes.h"
#include "../matIO/matio.h"

#ifndef WINDOWS
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <dlfcn.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #include <pthread.h>

    #define LOADLIBRARY(libname) dlopen(libname, RTLD_LAZY);
    #define FREELIBRARY(handle) dlclose(handle);
    #define GETFCNPTR dlsym
    #define CLOSESOCKET(sock) close(sock);

    typedef void* HANDLE;
#else
    #include <stdint.h>
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>

    #define LOADLIBRARY(libname) LoadLibrary(libname);
    #define FREELIBRARY(handle) FreeLibrary(handle);
    #define GETFCNPTR GetProcAddress
    #define CLOSESOCKET(sock) closesocket(sock);

    typedef DWORD pthread_t;
#endif // !WINDOWS

#define FALSE 0
#define TRUE 1
#define MAX_THREAD_COUNT 10
//#define NOLOG

#ifdef LOG

    #define PRINT(...)                                      \
                logfile = fopen(logfilepath, "a");          \
                sprintf(logbuf, __VA_ARGS__);               \
                fwrite(logbuf, 1, strlen(logbuf), logfile); \
                fclose(logfile);

#elif defined NOLOG

	#define PRINT(...)

#else

	#define PRINT(...) printf(__VA_ARGS__);

#endif

#define FMI_GET_FUNC_ADDR( fun )                                        \
                fmu->fun = (fun##TYPE*)GETFCNPTR(fmu->handle, #fun);    \
                if (fmu->fun == NULL)                                   \
                {                                                       \
                    PRINT("Load %s failed!\n",#fun);                    \
                    return -1;                                          \
                }

typedef int bool;

typedef struct {
	HANDLE handle;
	fmi2GetVersionTYPE *fmi2GetVersion;
	fmi2InstantiateTYPE *fmi2Instantiate;
	fmi2SetupExperimentTYPE *fmi2SetupExperiment;
	fmi2SetRealTYPE *fmi2SetReal;
	fmi2EnterInitializationModeTYPE *fmi2EnterInitializationMode;
	fmi2ExitInitializationModeTYPE *fmi2ExitInitializationMode;
	fmi2DoStepTYPE *fmi2DoStep;
	fmi2GetRealTYPE *fmi2GetReal;
	fmi2TerminateTYPE *fmi2Terminate;
	fmi2ResetTYPE *fmi2Reset;
	fmi2FreeInstanceTYPE *fmi2FreeInstance;
	fmi2Component fmuInstance;
} FMU_Instance;

typedef struct
{
	size_t noOfParam;
	fmi2Real parameter[100];
	fmi2ValueReference paramRefs[100];
	fmi2String paramNames[100];
	size_t noOfVars;
	fmi2Real var[100];
	fmi2ValueReference varRefs[100];
	fmi2String varNames[100];
} Variables;

typedef struct{
	pthread_t *threadID;
	unsigned int threadIndex;
	int serverSocket;
	bool *quitServer;
	bool persistentConnection;
	char *cd;
} ThreadArguments;

char header[512];
const char http_protocol[] =    "HTTP/1.1 200 OK\r\n"
                                "Server: WebFMU Server\r\n"
                                "Keep-Alive: timeout=20, max=50\r\n"
                                "Cache-Control: %s\r\n"
                                "Content-Length: %d\r\n"
                                "Content-Type: %s\r\n\r\n";

static FILE *logfile;
char logbuf[1024];
char logfilepath[256];

char getNextDelimiter(char **src, char *delimiter)
{
	while (**src != 0)
	{
		for (uint32_t i = 0; i < strlen(delimiter); i++)
		{
			if (**src == delimiter[i])
			{
				(*src)++;
				return delimiter[i];
			}
		}
		(*src)++;
	}

	return 0;
}

int findNextTokenLimited(char **src, char *token, char limit)
{
	while (**src < 65)
	{
		if (**src == 0) return -1;
		(*src)++;
	}

	char *start = *src;
	int len = 0;

	while (**src != limit)
	{
		if (**src == 0) return -1;
		len++;
		(*src)++;
	}
	memcpy(token, start, len);
	token[len] = 0;

	return 0;
}

int findNextToken(char **src, char *token)
{
	while (**src < 65)
	{
		if (**src == 0) return -1;
		(*src)++;
	}

	char *start = *src;
	int len = 0;

	while ((**src >= 48 && **src <= 57) || **src >= 65)
	{
		if (**src == 0) return -1;
		len++;
		(*src)++;
	}
	memcpy(token, start, len);
	token[len] = 0;

	return 0;
}

void *allocateMemory(size_t nobj, size_t size)
{
	return malloc(nobj*size);
}

void freeMemory(void* obj)
{
	free(obj);
}

void logger(fmi2ComponentEnvironment a, fmi2String b, fmi2Status c, fmi2String d, fmi2String e, ...)
{
	PRINT("Logger: %s %s %s\n", b, d, e);
}

fmi2CallbackFunctions callbacks = { logger, allocateMemory, freeMemory, NULL, NULL };

int initFMU(FMU_Instance *fmu, fmi2String instanceName, fmi2String guid, fmi2String fmuResourcesLocation, char *fmuFullFilePath)
{
	PRINT("Lade FMU: %s\n", fmuFullFilePath);
	fmu->handle = LOADLIBRARY(fmuFullFilePath);
	if (!fmu->handle)
	{
		PRINT("Load Library failed!\n");
		return -1;
	}

	FMI_GET_FUNC_ADDR(fmi2GetVersion)
	FMI_GET_FUNC_ADDR(fmi2Instantiate)
	FMI_GET_FUNC_ADDR(fmi2SetupExperiment)
	FMI_GET_FUNC_ADDR(fmi2SetReal)
	FMI_GET_FUNC_ADDR(fmi2EnterInitializationMode)
	FMI_GET_FUNC_ADDR(fmi2ExitInitializationMode)
	FMI_GET_FUNC_ADDR(fmi2DoStep)
	FMI_GET_FUNC_ADDR(fmi2GetReal)
	FMI_GET_FUNC_ADDR(fmi2Terminate)
	FMI_GET_FUNC_ADDR(fmi2Reset)
	FMI_GET_FUNC_ADDR(fmi2FreeInstance)

	PRINT("FMI-Version: %s\n", fmu->fmi2GetVersion());

	PRINT("GUID: %s\n", guid);
	fmu->fmuInstance = fmu->fmi2Instantiate(instanceName, fmi2CoSimulation, guid, fmuResourcesLocation, &callbacks, fmi2False, fmi2False);
	if (fmu->fmuInstance == NULL)
	{
		PRINT("Get fmu instance failed!\n");
		return -1;
	}
	PRINT("FMU instanciated.\n");
	PRINT("Handle ---> %p\n", (void*)fmu->handle);
	PRINT("FMU Instance ---> %p\n", (void*)fmu->fmuInstance);
	PRINT("FMU SetReal ---> %p\n", (void*)fmu->fmi2SetReal);

	return 0;
}

int sim_pt1(double t[], double x[], double tau, int i)
{
	double h = 0.1;
	double x_0 = 0;
	double dx;

	t[0] = 0;
	x[0] = x_0;
	dx = (1 - x[i]) / tau;
	t[i + 1] = t[i] + h;
	x[i + 1] = x[i] + h*dx;
	i++;

	return 0;
}

int simulate(FMU_Instance *fmu, char *resultFullFilePath, fmi2Real tStop, fmi2Real tOutStep, Variables *variables, char *htmlbuf)
{
	int i = 0;
	int j;
	FILE *matFile;
	fmi2Real tolerance = 0.000001;
	fmi2Real tStart = 0;
	fmi2Real tComm = tStart;
	fmi2Real tCommStep = 0.001; // FMU Exports duch OMC verwenden nur den Euler --> dort gibt es keine interne Schrittweite --> es wird nur die Kommunikationsschrittweite verwendet
	fmi2Real tNextOut = tStart;
	fmi2Status status = fmi2OK;
	double *time;
	double *out;
	unsigned int elementCount = (unsigned int)(tStop / tOutStep) + 1 + 1;

	time = malloc(elementCount * sizeof(double));
	out = malloc(elementCount * variables->noOfVars * sizeof(double));

	PRINT("Simulate FMU.\n");

	if (fmu->fmi2SetupExperiment(fmu->fmuInstance, fmi2True, tolerance, tStart, fmi2False, tStop) != fmi2OK)
	{
		PRINT("Experiment setup failed!\n");
		return -1;
	}

	if (fmu->fmi2SetReal(fmu->fmuInstance, variables->paramRefs, variables->noOfParam, variables->parameter) != fmi2OK)
	{
		PRINT("Set real failed!\n");
		return -1;
	}

	if (fmu->fmi2EnterInitializationMode(fmu->fmuInstance) != fmi2OK)
	{
		PRINT("EnterInitializationMode failed!\n");
		return -1;
	}

	if (fmu->fmi2ExitInitializationMode(fmu->fmuInstance) != fmi2OK)
	{
		PRINT("ExitInitializationMode failed!\n");
		return -1;
	}

	status = fmu->fmi2GetReal(fmu->fmuInstance, variables->varRefs, variables->noOfVars, variables->var);
	if (status != fmi2OK)
	{
		PRINT("Error getReal!\n");
	}
	time[i] = tComm;
	for (j = 0; j < variables->noOfVars; j++)
		out[j*elementCount + i] = variables->var[j];

	tNextOut += tOutStep;
	
	while (tComm < (tStop-0.00000001) && status == fmi2OK)
	{
		status = fmu->fmi2DoStep(fmu->fmuInstance, tComm, tCommStep, fmi2True);
		if (status != fmi2OK)
		{
			PRINT("Error doStep!\n");
		}
		tComm += tCommStep;

		status = fmu->fmi2GetReal(fmu->fmuInstance, variables->varRefs, variables->noOfVars, variables->var);
		if (status != fmi2OK)
		{
			PRINT("Error getReal!\n");
		}
		if (!(tComm < (tNextOut-0.00000001)))
		{
			i++;
			time[i] = tComm;
			for (j = 0; j < variables->noOfVars; j++)
				out[j*elementCount + i] = variables->var[j];

			tNextOut += tOutStep;
		}
	}
	
	matFile = fopen(resultFullFilePath, "wb");
	writeHeader("Created by WebFMU", matFile);
	writeDoubleMatrix("time", time, i + 1, 1, matFile);
	for (j = 0; j < variables->noOfVars; j++)
		writeDoubleMatrix((char*)variables->varNames[j], &out[j*elementCount], i + 1, 1, matFile);
	fclose(matFile);
	
	char *ptr = resultFullFilePath;
	char *tmp;
	char token[32];
	while (getNextDelimiter(&ptr, "/")) { tmp = ptr; }
	findNextToken(&tmp, token);
	sprintf(htmlbuf, "%s", "<table>");
	sprintf(htmlbuf+strlen(htmlbuf), "<tr><td>%s</td><td>%s</td></tr>", "Projektname", token);
	for (i = 0; i < variables->noOfParam; i++)
	{
		sprintf(htmlbuf + strlen(htmlbuf), "<tr><td>%s</td><td>%0.3lf</td></tr>", variables->paramNames[i], variables->parameter[i]);
	}
	sprintf(htmlbuf + strlen(htmlbuf), "<tr><td>%s</td><td>%0.3lf</td></tr>", "t_end", tComm);
	for (i = 0; i < variables->noOfVars; i++)
	{
		sprintf(htmlbuf + strlen(htmlbuf), "<tr><td>%s</td><td>%0.3lf</td></tr>", variables->varNames[i], variables->var[i]);
	}
	sprintf(htmlbuf + strlen(htmlbuf), "</table>");

	PRINT("Simulation succesfully terminated.\n");

	free(time);
	free(out);
	return 0;
}

int readFile(char *filename, int *dataSize, char **data, char *readMode)
{
	FILE *file;

	file = fopen(filename, readMode);
	if (file == NULL)
	{
		PRINT("Could not open the file!\n");
		return -1;
	}
	fseek(file, 0, SEEK_END);
	*dataSize = ftell(file);
	rewind(file);
	*data = malloc(*dataSize + 1);
	fread(*data, *dataSize, 1, file);
	(*data)[*dataSize] = 0;
	fclose(file);

	return 0;
}

void *connectionThread(void *argin)
{
	ThreadArguments *args = argin;
	int intVal;
	char *ptr;
	char token[256];
	bool quitConnection;
	int numbytes;
	char recvbuf[1024];
	unsigned int headerlen;
	char *databuf;
	char loadPath[256];
	int databuflen;
	char *sendbuf;
	int sendbuflen;
	char str[128];
	char linkModelParam[64];
	char fmuPath[] = "fmu/";
	char fmuFileName[64];
	char fmuFullFilePath[256];
	char resultFilePath[] = "data/";
	char resultFileName[64];
	char resultFullFilePath[256];
	fmi2String instanceName = "WebFMU";
	fmi2String guid = "{8c4e810f-3df3-4a00-8276-176fa3c9f9e0}";
	fmi2String fmuResourcesLocation = NULL;
	fmi2Real tstop = 1.0;
	fmi2Real tOutStep = 0.1;
	FMU_Instance fmu;
	Variables variables;

	PRINT("Thread %d started. ID = %d\n", args->threadIndex, (int)args->threadID[args->threadIndex]);
	strcpy(loadPath, args->cd);
	quitConnection = FALSE;
	while (!quitConnection || args->persistentConnection)
	{
		databuf = NULL;
		PRINT("receiving...\n\n");
		if ((numbytes = recv(args->serverSocket, recvbuf, 1024, 0)) < 1)
		{
			PRINT("receiving failed!\n");
			quitConnection = TRUE;
			break;
		}
		recvbuf[numbytes] = 0;
		PRINT("%d bytes received:\n%s\n\n", numbytes, recvbuf);
		if ((ptr = strstr(recvbuf, "\r\n\r\n")))
		{
			ptr += 4;
			headerlen = (unsigned int)(ptr - recvbuf);
			PRINT("Length of header: %d\n\n", headerlen);
		}

		if (strstr(recvbuf, "GET / HTTP/1.1"))
		{
			strcpy(loadPath + strlen(args->cd), "html/index.html");
			readFile(loadPath, &databuflen, &databuf, "r");
			sprintf(header, http_protocol, "max-age=3600", databuflen, "text/html");
		}
		else if (strstr(recvbuf, "GET /style.css"))
		{
			strcpy(loadPath + strlen(args->cd), "html/style.css");
			readFile(loadPath, &databuflen, &databuf, "r");
			sprintf(header, http_protocol, "max-age=3600", databuflen, "text/css");
		}
		else if (strstr(recvbuf, "GET /favicon"))
		{
			strcpy(loadPath + strlen(args->cd), "res/FMU_32x32.png");
			readFile(loadPath, &databuflen, &databuf, "rb");
			sprintf(header, http_protocol, "max-age=3600", databuflen, "image/apng");
		}
		else if (strstr(recvbuf, "GET /logo"))
		{
			strcpy(loadPath + strlen(args->cd), "res/FMU.svg");
			readFile(loadPath, &databuflen, &databuf, "r");
			sprintf(header, http_protocol, "max-age=3600", databuflen, "image/svg+xml");
		}
		else if (strstr(recvbuf, "GET /modelparameter"))
		{
			sprintf(str, "fmu/%s.xml", linkModelParam);
			strcpy(loadPath + strlen(args->cd), str);
			readFile(loadPath, &databuflen, &databuf, "r");
			sprintf(header, http_protocol, "no-store", databuflen, "text/xml");
		}
		else if (strstr(recvbuf, "GET /options?"))
		{
			ptr = recvbuf;
			getNextDelimiter(&ptr, "?");
			findNextToken(&ptr, token);

			databuf = malloc(1024);
			if (token[0] == 'c')
			{
				args->persistentConnection = FALSE;
				PRINT("set connection type = closed\n\n");
				strcpy(databuf, "<p>set connection type = closed</p>");
			}
			else if (token[0] == 'p')
			{
				args->persistentConnection = TRUE;
				PRINT("set connection type = persistent\n\n");
				strcpy(databuf, "<p>set connection type = persistent</p>");
			}
			else if (token[0] == 'q')
			{
				PRINT("Server quitted!\n");
				quitConnection = TRUE;
				*args->quitServer = TRUE;
				break;
			}
			else if (token[0] == 'h')
			{
				strcpy(databuf, "<p>Options:</p>");
				strcat(databuf, "<p>-h print this help</p>");
				strcat(databuf, "<p>-c set connection type = closed</p>");
				strcat(databuf, "<p>-p set connection type = persistent</p>");
				strcat(databuf, "<p>-q quit server</p>");
			}
			else
			{
				PRINT("Unknown command!\n");
				strcpy(databuf, "<p>Unknown command!</p>");
			}
			databuflen = (int)strlen(databuf);
			sprintf(header, http_protocol, "no-store", databuflen, "text/html");
		}
		else if (strstr(recvbuf, "GET /modelmenu"))
		{
			char fmuName[128];
			char token[128];

			databuf = malloc(1024);
			strcpy(databuf, "<select id = 'modelSelection' onchange = 'changeSelection()' style = 'width: 50%'>");
#ifndef WINDOWS
			DIR *dp;
			struct dirent *ep;

			strcpy(loadPath + strlen(args->cd), "fmu");
			dp = opendir(loadPath);
			if (dp != NULL)
			{
				while ((ep = readdir(dp)))
				{
					ptr = ep->d_name;
					if (findNextToken(&ptr, fmuName)) continue;
					if (findNextToken(&ptr, token)) continue;
					if (!strcmp(token, "xml")) sprintf(databuf + strlen(databuf), "<option>%s</option>", fmuName);
				}
				(void)closedir(dp);
			}
			else
				PRINT("Couldn't open the directory!\n");
#else
			HANDLE hFind = INVALID_HANDLE_VALUE;
			WIN32_FIND_DATA ffd;

			strcpy(loadPath + strlen(args->cd), "fmu/*");
			hFind = FindFirstFile(loadPath, &ffd);
			if (hFind != INVALID_HANDLE_VALUE)
			{
				do
				{
					PRINT("  %s\n", ffd.cFileName);
					ptr = ffd.cFileName;
					if (findNextToken(&ptr, fmuName)) continue;
					if (findNextToken(&ptr, token)) continue;
					if (!strcmp(token, "xml")) sprintf(databuf + strlen(databuf), "<option>%s</option>", fmuName);
				} while (FindNextFile(hFind, &ffd) != 0);

				FindClose(hFind);
			}
			else
				PRINT("Couldn't open the directory!\n");
#endif
			strcpy(databuf + strlen(databuf), "</select>");
			databuflen = (int)strlen(databuf);
			sprintf(header, http_protocol, "no-store", databuflen, "text/html");
		}
		else if (strstr(recvbuf, "GET /results"))
		{
#ifndef WINDOWS
			DIR *dp;
			struct dirent *ep;
			struct stat fileStat;
			char fmuName[128];
			char token[128];

			databuf = malloc(2048);
			strcpy(databuf, "<html><table>");

			strcpy(loadPath + strlen(args->cd), "data");
			dp = opendir(loadPath);
			if (dp != NULL)
			{
				while ((ep = readdir(dp)))
				{
					ptr = ep->d_name;
					if (findNextToken(&ptr, fmuName)) continue;
					if (findNextToken(&ptr, token)) continue;

					if (!strcmp(token, "mat"))
					{
						sprintf(fmuName, "%s.%s", fmuName, token);
						sprintf(str, "data/%s", fmuName);
						strcpy(loadPath + strlen(args->cd), str);
						stat(loadPath, &fileStat);
						sprintf(databuf + strlen(databuf), "<tr><td><a href='load?/data/%s' download='%s'>%s</a></td><td>%u</td><td>%s</td></tr>", fmuName, fmuName, fmuName, (unsigned int)fileStat.st_size, ctime(&fileStat.st_mtime));
					}
				}
				(void)closedir(dp);
			}
			else
				PRINT("Couldn't open the directory!\n");
#endif
			strcpy(databuf + strlen(databuf), "</table></html>");
			databuflen = (int)strlen(databuf);
			sprintf(header, http_protocol, "no-store", databuflen, "text/html");
		}
		else if (strstr(recvbuf, "GET /load?"))
		{
			ptr = recvbuf;
			*str = 0;
			getNextDelimiter(&ptr, "?");
			findNextToken(&ptr, token);
			if (*ptr != '.')
			{
				sprintf(str, "%s/", token);
				findNextToken(&ptr, token);
			}
			sprintf(str + strlen(str), "%s.", token);
			findNextToken(&ptr, token);
			sprintf(str + strlen(str), "%s", token);
			if (getNextDelimiter(&ptr, "&/") == '&')
			{
				findNextToken(&ptr, linkModelParam);
			}
			strcpy(loadPath + strlen(args->cd), str);
			if (strstr(str, "xml"))
			{
				readFile(loadPath, &databuflen, &databuf, "r");
				sprintf(header, http_protocol, "no-store", databuflen, "text/xml");
			}
			else if (strstr(str, "txt"))
			{
				readFile(loadPath, &databuflen, &databuf, "r");
				sprintf(header, http_protocol, "no-store", databuflen, "text/plain");
			}
			else if (strstr(str, "svg"))
			{
				readFile(loadPath, &databuflen, &databuf, "r");
				sprintf(header, http_protocol, "max-age=3600", databuflen, "image/svg+xml");
			}
			else if (strstr(str, "bin") || strstr(str, "mat"))
			{
				readFile(loadPath, &databuflen, &databuf, "rb");
				sprintf(header, http_protocol, "no-store", databuflen, "application/octet-stream");
			}
			else
			{
				readFile(loadPath, &databuflen, &databuf, "r");
				sprintf(header, http_protocol, "no-store", databuflen, "text/html");
			}
		}
		else if (strstr(recvbuf, "POST /sim"))
		{
			ptr = strstr(recvbuf, "Content-Length");
			if (!findNextTokenLimited(&ptr, token, ':'))
			{
				PRINT("%s: ", token);
				ptr++;
				sscanf(ptr, "%d", &intVal);
				PRINT("%d\n\n", intVal);
				if (numbytes < ((int)headerlen + intVal))
				{
					if ((numbytes = recv(args->serverSocket, recvbuf, 1024, 0)) < 1)
					{
						PRINT("receiving failed!\n");
						quitConnection = TRUE;
						break;
					}
					recvbuf[numbytes] = 0;
					PRINT("!!!!!!!!!!!!! %d bytes of content received:\n%s\n\n", numbytes, recvbuf);
				}
			}

			ptr = strstr(recvbuf, "projname");
			if (ptr)
			{
				variables.noOfParam = 0;
				variables.noOfVars = 0;
				while (!findNextToken(&ptr, token))
				{
					if (!strcmp(token, "fmuname"))
					{
						findNextToken(&ptr, token);
						sprintf(fmuFileName, "%s%s", token, ".so");
					}
					else if (!strcmp(token, "projname"))
					{
						findNextToken(&ptr, token);
						sprintf(resultFileName, "%s%s", token, ".mat");
					}
					else if (!strcmp(token, "tstop"))
					{
						getNextDelimiter(&ptr, "=");
						sscanf(ptr, "%lf", &tstop);
					}
					else if (!strcmp(token, "tOutStep"))
					{
						getNextDelimiter(&ptr, "=");
						sscanf(ptr, "%lf", &tOutStep);
					}
					else if (!strcmp(token, "pname"))
					{
						ptr++;
						sscanf(ptr, "%d", &intVal);
						findNextToken(&ptr, token);
						variables.paramNames[intVal] = malloc(strlen(token));
						strcpy((char*)variables.paramNames[intVal], token);
						variables.noOfParam++;
					}
					else if (!strcmp(token, "pref"))
					{
						ptr++;
						sscanf(ptr, "%d", &intVal);
						getNextDelimiter(&ptr, "=");
						sscanf(ptr, "%d", &variables.paramRefs[intVal]);
					}
					else if (!strcmp(token, "pval"))
					{
						ptr++;
						sscanf(ptr, "%d", &intVal);
						getNextDelimiter(&ptr, "=");
						sscanf(ptr, "%lf", &variables.parameter[intVal]);
					}
					else if (!strcmp(token, "vname"))
					{
						ptr++;
						sscanf(ptr, "%d", &intVal);
						findNextTokenLimited(&ptr, token, '&');
						variables.varNames[intVal] = malloc(strlen(token));
						strcpy((char*)variables.varNames[intVal], token);
						variables.noOfVars++;
					}
					else if (!strcmp(token, "vref"))
					{
						ptr++;
						sscanf(ptr, "%d", &intVal);
						getNextDelimiter(&ptr, "=");
						sscanf(ptr, "%d", &variables.varRefs[intVal]);
					}
				}
				sprintf(fmuFullFilePath, "%s%s%s", args->cd, fmuPath, fmuFileName);
				sprintf(resultFullFilePath, "%s%s%s", args->cd, resultFilePath, resultFileName);
				databuf = malloc(5000);
				if (!initFMU(&fmu, instanceName, guid, fmuResourcesLocation, fmuFullFilePath))
				{
					if (!simulate(&fmu, resultFullFilePath, tstop, tOutStep, &variables, databuf))
					{
						sprintf(databuf + strlen(databuf), "<p>FMU %s instanciated.</p><p>Result File %s created.</p>"
							"<p>Simulation succesfully terminated.</p><div id='plot'></div>", fmuFileName, resultFileName);
					}
					else
					{
						strcat(databuf, "<p>Simulation Error!</p><p>Simulation failed!</p>");
					}
					//fmi2Terminate(fmuInstance);
					//fmi2Reset(fmuInstance);
					fmu.fmi2FreeInstance(fmu.fmuInstance);
                    FREELIBRARY(fmu.handle)
				}
				else
				{
					sprintf(databuf, "<p>FMU %s could not be instanciated!</p><p>Simulation failed!</p>", fmuFileName);
				}
				databuflen = (int)strlen(databuf);
				sprintf(header, http_protocol, "no-store", databuflen, "text/html");
			}
			else
			{
				databuf = malloc(128);
				strcpy(databuf, "<p style='font-size:100px;text-align:center'>Incorrect Post Data!</p>");
				databuflen = (int)strlen(databuf);
				sprintf(header, http_protocol, "no-store", databuflen, "text/html");
			}
		}
		else
		{
			PRINT("No Content!\n");
			databuf = malloc(128);
			strcpy(databuf, "<p style='font-size:100px;text-align:center'>What!</p>");
			databuflen = (int)strlen(databuf);
			sprintf(header, http_protocol, "no-store", databuflen, "text/html");
		}
		if (!databuf) break;
		sendbuflen = (int)strlen(header) + databuflen;
		sendbuf = malloc(sendbuflen);
		strcpy(sendbuf, header);
		memcpy(sendbuf + strlen(header), databuf, databuflen);
		if ((numbytes = send(args->serverSocket, sendbuf, sendbuflen, 0)) < 1)
		{
			PRINT("Senden failed!\n");
			quitConnection = TRUE;
			free(sendbuf);
			free(databuf);
			break;
		}
		PRINT("%d Bytes sent.\n", numbytes);
		PRINT("Header:\n%s\n\n", header);

		free(sendbuf);
		free(databuf);
		if (!args->persistentConnection) quitConnection = TRUE;
	}
    CLOSESOCKET(args->serverSocket)
	PRINT("Thread %d closed. ID = %d\n", args->threadIndex, (int)args->threadID[args->threadIndex]);
	args->threadID[args->threadIndex] = -1;

	return NULL;
}

int main(int argc, char *argv[])
{
	int acceptSocket, serverSocket;
	bool persistentConnection = TRUE;
	struct sockaddr_in addr;
	struct sockaddr_in client_addr;
	socklen_t addrlen;
	char *str_client_ip;
	int port = 80;
	bool quitServer = FALSE;
	int i;
	char *cd;
	char *ptr;
	char loadPath[256];
	ThreadArguments args[MAX_THREAD_COUNT];
	pthread_t threadID[MAX_THREAD_COUNT];
	unsigned int threadIndex=0;
    int status = 1;

	cd = argv[0];
	ptr = cd;
	char *tmp = ptr;
	while (getNextDelimiter(&ptr, "/")) { tmp = ptr; }
	*tmp = 0;
	strcpy(loadPath, cd);
	strcpy(loadPath + strlen(cd), "log.txt");
	strcpy(logfilepath, loadPath);
	logfile = fopen(logfilepath, "w");
	fclose(logfile);
	PRINT("%s\n\n", loadPath);
	memset(threadID, -1, MAX_THREAD_COUNT*sizeof(int));
	//threadID[0] = -1;
	for (threadIndex = 0; threadIndex < MAX_THREAD_COUNT; threadIndex++) PRINT("%d   ", (int)threadID[threadIndex]);
	PRINT("\n");

	PRINT("Web FMU Server\n\n");

	for (i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "-c"))
		{
			persistentConnection = FALSE;
			PRINT("set connection type = closed\n\n");
		}
		else if (!strcmp(argv[i], "-p"))
		{
			persistentConnection = TRUE;
			PRINT("set connection type = persistent\n\n");
		}
		else if (!strcmp(argv[i], "-port"))
		{
			sscanf(argv[i + 1], "%d", &port);
			PRINT("set port = %d\n", port);
			i++;
		}
		else if (!strcmp(argv[i], "-h"))
		{
			printf("Options:\n");
			printf("\t-h\t\tprint this help\n");
			printf("\t-c\t\tset connection type = closed\n");
			printf("\t-p\t\tset connection type = persistent\n");
			printf("\t-port\t\tset port\n");
			exit(1);
		}
		else
		{
			printf("Unknown command!\n");
			exit(1);
		}
	}

#ifdef WINDOWS
    WSADATA wsaData;

    // Initialize Winsock
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        PRINT("WSAStartup failed: %d\n", iResult);
        return 1;
    }
#endif

	acceptSocket = (int)socket(AF_INET, SOCK_STREAM, 0);
	if (acceptSocket<0)
	{
		PRINT("Failed to acceptSocket!\n");
		return 1;
	}
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(acceptSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0)
	{
		PRINT("Bind failed!\n");
        CLOSESOCKET(acceptSocket)
		return 1;
	}
	PRINT("Bind Socket with Port: %d\n", port);

	if (listen(acceptSocket, MAX_THREAD_COUNT)<0)
	{
		PRINT("Listen failed!\n");
        CLOSESOCKET(acceptSocket)
		return 1;
	}

	PRINT("Server started ...\n");

	while (!quitServer)
	{
		PRINT("Wait for connection...\n\n");
		serverSocket = (int)accept(acceptSocket, (struct sockaddr*)&client_addr, &addrlen);
		if (serverSocket<0)
		{
			PRINT("Connection failed!\n");
            CLOSESOCKET(acceptSocket)
			return 1;
		}
		str_client_ip = inet_ntoa(client_addr.sin_addr);
		PRINT("Connection accepted.\n");
		PRINT("Connected with %s.\n\n", str_client_ip);

		for (threadIndex = 0; threadIndex < MAX_THREAD_COUNT; threadIndex++)
		{
			if (threadID[threadIndex] == -1)
			{
				args[threadIndex].threadID = threadID;
				args[threadIndex].threadIndex = threadIndex;
				args[threadIndex].cd = cd;
				args[threadIndex].persistentConnection = persistentConnection;
				args[threadIndex].quitServer = &quitServer;
				args[threadIndex].serverSocket = serverSocket;

#ifndef WINDOWS
				status=pthread_create(&threadID[threadIndex], NULL, connectionThread, &args[threadIndex]);
				if (status != 0)
				{
					PRINT("Thread creation failed!\n");
					threadID[threadIndex] = -1;
				}
#else
                HANDLE threadHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)connectionThread, &args[threadIndex], 0, &threadID[threadIndex]);
                if (threadHandle == NULL)
                {
                    PRINT("Thread creation failed!\n");
                    threadID[threadIndex] = -1;
                    status = 1;
                }
                else
                    status = 0;
#endif
				break;
			}
		}
		if (status != 0)
		{
			PRINT("No Threads availlable!\n");
            CLOSESOCKET(serverSocket)
		}
		for (threadIndex = 0; threadIndex < MAX_THREAD_COUNT; threadIndex++) PRINT("%d   ", (int)threadID[threadIndex]);
		PRINT("\n");
		status = 1;
	}

    CLOSESOCKET(acceptSocket)

	return 0;
}