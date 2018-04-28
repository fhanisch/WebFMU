/// @date: 01.12.2017
/// @author: fh

/*
	compile: gcc -std=c11 -o webfmuserver server.c -ldl
	Wichtig: Cache-Control im Header beachten !!! --> Wann müssen welche Seiten aktualisiert werden?
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include "fmi2FunctionTypes.h"
#include "../matIO/matio.h"

#define FALSE 0
#define TRUE 1

#define LOG
#ifdef LOG

#	define PRINT(...)										\
				logfile = fopen(logfilepath, "a");			\
				sprintf(logbuf, __VA_ARGS__);				\
				fwrite(logbuf, 1, strlen(logbuf), logfile);	\
				fclose(logfile);

#elif defined NOLOG

#	define PRINT(...)

#else

#	define PRINT(...) printf(__VA_ARGS__);

#endif

#define FMI_GET_FUNC_ADDR( fun )						\
			fun = (fun##TYPE*)dlsym(handle, #fun);		\
			if (fun == NULL)							\
			{											\
				PRINT("Load %s failed!\n",#fun);		\
				return -1;								\
			}

typedef int bool;

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

char header[512];
const char http_protocol[] =	"HTTP/1.1 200 OK\r\n"
								"Server: WebFMU Server\r\n"
								"Keep-Alive: timeout=20, max=50\r\n"
								"Cache-Control: %s\r\n"
								"Content-Length: %d\r\n"
								"Content-Type: %s\r\n\r\n";

static void *handle;
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
static fmi2Component fmuInstance;
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

int initFMU(fmi2String instanceName, fmi2String guid, fmi2String fmuResourcesLocation, char *fmuFullFilePath)
{
	PRINT("Lade FMU: %s\n", fmuFullFilePath);
	handle = dlopen(fmuFullFilePath, RTLD_LAZY);
	if (!handle)
	{
		PRINT("dlopen failed!\n");
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

	PRINT("FMI-Version: %s\n", fmi2GetVersion());

	PRINT("GUID: %s\n", guid);
	fmuInstance = fmi2Instantiate(instanceName, fmi2CoSimulation, guid, fmuResourcesLocation, &callbacks, fmi2False, fmi2False);
	if (fmuInstance == NULL)
	{
		PRINT("Get fmu instance failed!\n");
		return -1;
	}
	PRINT("FMU instanciated.\n");

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

int simulate(char *resultFullFilePath, fmi2Real tStop, fmi2Real tOutStep, Variables *variables, char *htmlbuf)
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

	if (fmi2SetupExperiment(fmuInstance, fmi2True, tolerance, tStart, fmi2False, tStop) != fmi2OK)
	{
		PRINT("Experiment setup failed!\n");
		return -1;
	}

	if (fmi2SetReal(fmuInstance, variables->paramRefs, variables->noOfParam, variables->parameter) != fmi2OK)
	{
		PRINT("Set real failed!\n");
		return -1;
	}

	if (fmi2EnterInitializationMode(fmuInstance) != fmi2OK)
	{
		PRINT("EnterInitializationMode failed!\n");
		return -1;
	}

	if (fmi2ExitInitializationMode(fmuInstance) != fmi2OK)
	{
		PRINT("ExitInitializationMode failed!\n");
		return -1;
	}

	status = fmi2GetReal(fmuInstance, variables->varRefs, variables->noOfVars, variables->var);
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
		status = fmi2DoStep(fmuInstance, tComm, tCommStep, fmi2True);
		if (status != fmi2OK)
		{
			PRINT("Error doStep!\n");
		}
		tComm += tCommStep;

		status = fmi2GetReal(fmuInstance, variables->varRefs, variables->noOfVars, variables->var);
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
		printf("Could not open the file!\n");
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

int main(int argc, char *argv[])
{
	int acceptSocket, serverSocket;
	struct sockaddr_in addr;
	struct sockaddr_in client_addr;
	socklen_t addrlen;
	char *str_client_ip;
	int port = 80;
	bool quitServer = FALSE;
	bool quitConnection;
	bool persistentConnection = TRUE;
	char str[128];
	char recvbuf[1024];
	unsigned int headerlen;
	char *sendbuf;
	char *databuf;
	int sendbuflen;
	int databuflen;
	int numbytes;
	int i;
	char loadPath[256];
	int loadPathLen;
	char fmuPath[] = "fmu/";
	char fmuFileName[64];
	char fmuFullFilePath[256];
	char resultFilePath[] = "data/";
	char resultFileName[64];
	char resultFullFilePath[256];
	char linkModelParam[64];
	fmi2Real tstop = 1.0;
	fmi2Real tOutStep = 0.1;
	Variables variables;
	char token[256];
	fmi2String instanceName = "WebFMU";
	fmi2String guid = "{8c4e810f-3df3-4a00-8276-176fa3c9f9e0}";
	fmi2String fmuResourcesLocation = NULL;
	char *ptr;

	ptr = argv[0];
	char *tmp = ptr;
	while (getNextDelimiter(&ptr, "/")) { tmp = ptr; }
	*tmp = 0;
	strcpy(loadPath, argv[0]);
	loadPathLen = strlen(loadPath);
	strcpy(loadPath + loadPathLen, "log.txt");
	strcpy(logfilepath, loadPath);
	logfile = fopen(logfilepath, "w");
	fclose(logfile);
	//printf("%s\n\n", loadPath);

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

	acceptSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (acceptSocket<0)
	{
		printf("Failed to acceptSocket!\n");
		return 1;
	}
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(acceptSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0)
	{
		printf("Bind failed!\n");
		close(acceptSocket);
		return 1;
	}
	PRINT("Bind Socket with Port: %d\n", port);

	if (listen(acceptSocket, 10)<0)
	{
		printf("Listen failed!\n");
		close(acceptSocket);
		return 1;
	}

	PRINT("Server started ...\n");

	while (!quitServer)
	{
		PRINT("Wait for connection...\n\n");
		serverSocket = accept(acceptSocket, (struct sockaddr*)&client_addr, &addrlen);
		if (serverSocket<0)
		{
			printf("Connection failed!\n");
			close(acceptSocket);
			return 1;
		}
		str_client_ip = inet_ntoa(client_addr.sin_addr);
		PRINT("Connection accepted.\n");
		PRINT("Connected with %s.\n\n", str_client_ip);

		quitConnection = FALSE;
		while (!quitConnection || persistentConnection)
		{
			databuf = NULL;
			PRINT("receiving...\n\n");
			if ((numbytes = recv(serverSocket, recvbuf, 1024, 0)) < 1)
			{
				PRINT("receiving failed!\n");
				quitConnection = TRUE;
				break;
			}
			recvbuf[numbytes] = 0;
			PRINT("%d bytes received:\n%s\n\n", numbytes, recvbuf);
			if (ptr = strstr(recvbuf, "\r\n\r\n"))
			{
				ptr += 4;
				headerlen = ptr - recvbuf;
				PRINT("Length of header: %d\n\n", headerlen);
			}

			if (strstr(recvbuf, "GET / HTTP/1.1"))
			{
				strcpy(loadPath + loadPathLen, "html/index.html");
				readFile(loadPath, &databuflen, &databuf, "r");
				sprintf(header, http_protocol, "max-age=3600", databuflen, "text/html");
			}
			else if (strstr(recvbuf, "GET /style.css"))
			{
				strcpy(loadPath + loadPathLen, "html/style.css");
				readFile(loadPath, &databuflen, &databuf, "r");
				sprintf(header, http_protocol, "max-age=3600", databuflen, "text/css");
			}
			else if (strstr(recvbuf, "GET /favicon"))
			{
				strcpy(loadPath + loadPathLen, "res/FMU_32x32.png");
				readFile(loadPath, &databuflen, &databuf, "rb");
				sprintf(header, http_protocol, "max-age=3600", databuflen, "image/apng");
			}
			else if (strstr(recvbuf, "GET /logo"))
			{
				strcpy(loadPath + loadPathLen, "res/FMU.svg");
				readFile(loadPath, &databuflen, &databuf, "r");
				sprintf(header, http_protocol, "max-age=3600", databuflen, "image/svg+xml");
			}
			else if (strstr(recvbuf, "GET /modelparameter"))
			{
				sprintf(str, "fmu/%s.xml", linkModelParam);
				strcpy(loadPath + loadPathLen, str);
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
					persistentConnection = FALSE;
					printf("set connection type = closed\n\n");
					strcpy(databuf, "<p>set connection type = closed</p>");
				}
				else if (token[0] == 'p')
				{
					persistentConnection = TRUE;
					printf("set connection type = persistent\n\n");
					strcpy(databuf, "<p>set connection type = persistent</p>");
				}
				else if (token[0] == 'q')
				{
					printf("Server quitted!\n");
					quitConnection = TRUE;
					quitServer = TRUE;
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
					printf("Unknown command!\n");
					strcpy(databuf, "<p>Unknown command!</p>");
				}
				databuflen = strlen(databuf);
				sprintf(header, http_protocol, "no-store", databuflen, "text/html");
			}
			else if (strstr(recvbuf, "GET /modelmenu"))
			{
				DIR *dp;
				struct dirent *ep;
				char fmuName[128];
				char token[128];

				databuf = malloc(1024);
				strcpy(databuf, "<select id = 'modelSelection' onchange = 'changeSelection()' style = 'width: 50%'>");

				strcpy(loadPath + loadPathLen, "fmu");
				dp = opendir(loadPath);
				if (dp != NULL)
				{
					while (ep = readdir(dp))
					{
						ptr = ep->d_name;
						if (findNextToken(&ptr, fmuName)) continue;
						if (findNextToken(&ptr, token)) continue;
						if (!strcmp(token, "xml")) sprintf(databuf + strlen(databuf), "<option>%s</option>", fmuName);
					}
					(void)closedir(dp);
				}
				else
					printf("Couldn't open the directory!\n");

				strcpy(databuf + strlen(databuf), "</select>");
				databuflen = strlen(databuf);
				sprintf(header, http_protocol, "no-store", databuflen, "text/html");
			}
			else if (strstr(recvbuf, "GET /results"))
			{
				DIR *dp;
				struct dirent *ep;
				struct stat fileStat;
				char fmuName[128];
				char token[128];

				databuf = malloc(2048);
				strcpy(databuf, "<html><table>");

				strcpy(loadPath + loadPathLen, "data");
				dp = opendir(loadPath);
				if (dp != NULL)
				{
					while (ep = readdir(dp))
					{
						ptr = ep->d_name;
						if (findNextToken(&ptr, fmuName)) continue;
						if (findNextToken(&ptr, token)) continue;
	
						if (!strcmp(token, "mat"))
						{
							sprintf(fmuName, "%s.%s", fmuName, token);
							sprintf(str, "data/%s", fmuName);
							strcpy(loadPath + loadPathLen, str);
							stat(loadPath, &fileStat);
							sprintf(databuf + strlen(databuf), "<tr><td><a href='load?/data/%s' download='%s'>%s</a></td><td>%d</td><td>%s</td></tr>", fmuName, fmuName, fmuName, fileStat.st_size,ctime(&fileStat.st_mtime));
						}
					}
					(void)closedir(dp);
				}
				else
					printf("Couldn't open the directory!\n");

				strcpy(databuf + strlen(databuf), "</table></html>");
				databuflen = strlen(databuf);
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
				strcpy(loadPath + loadPathLen, str);
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
					sscanf(ptr, "%d", &i);
					PRINT("%d\n\n", i);
					if (numbytes < (headerlen + i))
					{
						if ((numbytes = recv(serverSocket, recvbuf, 1024, 0)) < 1)
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
							sscanf(ptr, "%d", &i);
							findNextToken(&ptr, token);
							variables.paramNames[i] = malloc(strlen(token));
							strcpy((char*)variables.paramNames[i], token);
							variables.noOfParam++;
						}
						else if (!strcmp(token, "pref"))
						{
							ptr++;
							sscanf(ptr, "%d", &i);
							getNextDelimiter(&ptr, "=");
							sscanf(ptr, "%d", &variables.paramRefs[i]);
						}
						else if (!strcmp(token, "pval"))
						{
							ptr++;
							sscanf(ptr, "%d", &i);
							getNextDelimiter(&ptr, "=");
							sscanf(ptr, "%lf", &variables.parameter[i]);
						}
						else if (!strcmp(token, "vname"))
						{
							ptr++;
							sscanf(ptr, "%d", &i);
							findNextTokenLimited(&ptr, token, '&');
							variables.varNames[i] = malloc(strlen(token));
							strcpy((char*)variables.varNames[i], token);
							variables.noOfVars++;
						}
						else if (!strcmp(token, "vref"))
						{
							ptr++;
							sscanf(ptr, "%d", &i);
							getNextDelimiter(&ptr, "=");
							sscanf(ptr, "%d", &variables.varRefs[i]);
						}
					}
					sprintf(fmuFullFilePath, "%s%s%s", argv[0], fmuPath, fmuFileName);
					sprintf(resultFullFilePath, "%s%s%s", argv[0], resultFilePath, resultFileName);
					databuf = malloc(5000);
					if (!initFMU(instanceName, guid, fmuResourcesLocation, fmuFullFilePath))
					{
						if (!simulate(resultFullFilePath, tstop, tOutStep, &variables, databuf))
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
						fmi2FreeInstance(fmuInstance);
						dlclose(handle);
					}
					else
					{
						sprintf(databuf, "<p>FMU %s could not be instanciated!</p><p>Simulation failed!</p>", fmuFileName);
					}
					databuflen = strlen(databuf);
					sprintf(header, http_protocol, "no-store", databuflen, "text/html");
				}
				else
				{
					databuf = malloc(128);
					strcpy(databuf, "<p style='font-size:100px;text-align:center'>Incorrect Post Data!</p>");
					databuflen = strlen(databuf);
					sprintf(header, http_protocol, "no-store", databuflen, "text/html");
				}
			}
			else
			{
				printf("No Content!\n");
				databuf = malloc(128);
				strcpy(databuf, "<p style='font-size:100px;text-align:center'>What!</p>");
				databuflen = strlen(databuf);
				sprintf(header, http_protocol, "no-store", databuflen, "text/html");
			}
			if (!databuf) break;
			sendbuflen = strlen(header) + databuflen;
			sendbuf = malloc(sendbuflen);
			strcpy(sendbuf, header);
			memcpy(sendbuf + strlen(header), databuf, databuflen);
			if ((numbytes = send(serverSocket, sendbuf, sendbuflen, 0)) < 1)
			{
				printf("Senden failed!\n");
				quitConnection = TRUE;
				free(sendbuf);
				free(databuf);
				break;
			}
			PRINT("%d Bytes sent.\n", numbytes);
			PRINT("Header:\n%s\n\n", header);

			free(sendbuf);
			free(databuf);
			if (!persistentConnection) quitConnection = TRUE;
		}
		close(serverSocket);
	}

	close(acceptSocket);

	return 0;
}