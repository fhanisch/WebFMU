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

#define FALSE 0
#define TRUE 1

#define FMI_GET_FUNC_ADDR( fun )						\
			fun = (fun##TYPE*)dlsym(handle, #fun);		\
			if (fun == NULL)							\
			{											\
				sprintf(str,"Load %s failed!\n",#fun);	\
				fwrite(str, 1, strlen(str), logfile);	\
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
	char str[256];
	sprintf(str, "Logger: %s %s %s\n", b, d, e);
	fwrite(str, 1, strlen(str), logfile);
}

fmi2CallbackFunctions callbacks = { logger, allocateMemory, freeMemory, NULL, NULL };

int initFMU(fmi2String instanceName, fmi2String guid, fmi2String fmuResourcesLocation, char *fmuFullFilePath)
{
	char str[128];
	sprintf(str, "Lade FMU: %s\n", fmuFullFilePath);
	fwrite(str, 1, strlen(str), logfile);
	handle = dlopen(fmuFullFilePath, RTLD_LAZY);
	if (!handle)
	{
		strcpy(str, "dlopen failed!\n");
		fwrite(str, 1, strlen(str), logfile);
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

	sprintf(str, "FMI-Version: %s\n", fmi2GetVersion());
	fwrite(str, 1, strlen(str), logfile);

	sprintf(str, "GUID: %s\n", guid);
	fwrite(str, 1, strlen(str), logfile);
	fmuInstance = fmi2Instantiate(instanceName, fmi2CoSimulation, guid, fmuResourcesLocation, &callbacks, fmi2False, fmi2False);
	if (fmuInstance == NULL)
	{
		strcpy(str, "Get fmu instance failed!\n");
		fwrite(str, 1, strlen(str), logfile);
		return -1;
	}
	strcpy(str, "FMU instanciated.\n");
	fwrite(str, 1, strlen(str), logfile);
	//printf("%s", str);

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
	char str[128];
	FILE *result;
	fmi2Real tolerance = 0.000001;
	fmi2Real tStart = 0;
	fmi2Real tComm = tStart;
	fmi2Real tCommStep = 0.001; // FMU Exports duch OMC verwenden nur den Euler --> dort gibt es keine interne Schrittweite --> es wird nur die Kommunikationsschrittweite verwendet
	fmi2Real tNextOut = tStart;
	fmi2Status status = fmi2OK;

	result = fopen(resultFullFilePath, "w");

	strcpy(str, "Simulate FMU.\n");
	fwrite(str, 1, strlen(str), logfile);

	sprintf(str, "%s,%s", "index", "time");
	fwrite(str, 1, strlen(str), result);
	for (j = 0; j < variables->noOfVars; j++)
	{
		sprintf(str, ",%s", variables->varNames[j]);
		fwrite(str, 1, strlen(str), result);
	}
	fwrite("\n", 1, 1, result);

	if (fmi2SetupExperiment(fmuInstance, fmi2True, tolerance, tStart, fmi2False, tStop) != fmi2OK)
	{
		sprintf(str, "Experiment setup failed!\n");
		fwrite(str, 1, strlen(str), logfile);
		fclose(logfile);
		return -1;
	}

	if (fmi2SetReal(fmuInstance, variables->paramRefs, variables->noOfParam, variables->parameter) != fmi2OK)
	{
		sprintf(str, "Set real failed!\n");
		fwrite(str, 1, strlen(str), logfile);
		fclose(logfile);
		return -1;
	}

	if (fmi2EnterInitializationMode(fmuInstance) != fmi2OK)
	{
		sprintf(str, "EnterInitializationMode failed!\n");
		fwrite(str, 1, strlen(str), logfile);
		fclose(logfile);
		return -1;
	}

	if (fmi2ExitInitializationMode(fmuInstance) != fmi2OK)
	{
		sprintf(str, "ExitInitializationMode failed!\n");
		fwrite(str, 1, strlen(str), logfile);
		fclose(logfile);
		return -1;
	}

	status = fmi2GetReal(fmuInstance, variables->varRefs, variables->noOfVars, variables->var);
	if (status != fmi2OK)
	{
		sprintf(str, "Error getReal!\n");
		fwrite(str, 1, strlen(str), logfile);
	}
	sprintf(str, "%d,%0.3f", i, tComm);
	fwrite(str, 1, strlen(str), result);
	for (j = 0; j < variables->noOfVars; j++)
	{
		sprintf(str, ",%0.3f", variables->var[j]);
		fwrite(str, 1, strlen(str), result);
	}
	fwrite("\n", 1, 1, result);
	tNextOut += tOutStep;
	
	while (tComm < (tStop-0.00000001) && status == fmi2OK)
	{
		status = fmi2DoStep(fmuInstance, tComm, tCommStep, fmi2True);
		if (status != fmi2OK)
		{
			sprintf(str, "Error doStep!\n");
			fwrite(str, 1, strlen(str), logfile);
		}
		tComm += tCommStep;

		status = fmi2GetReal(fmuInstance, variables->varRefs, variables->noOfVars, variables->var);
		if (status != fmi2OK)
		{
			sprintf(str, "Error getReal!\n");
			fwrite(str, 1, strlen(str), logfile);
		}
		if (!(tComm < (tNextOut-0.00000001)))
		{
			i++;
			sprintf(str, "%d,%0.3f", i, tComm);
			fwrite(str, 1, strlen(str), result);
			for (j = 0; j < variables->noOfVars; j++)
			{
				sprintf(str, ",%0.3f", variables->var[j]);
				fwrite(str, 1, strlen(str), result);
			}
			fwrite("\n", 1, 1, result);
			tNextOut += tOutStep;
		}
	}
	
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

	strcpy(str, "Simulation succesfully terminated.\n");
	fwrite(str, 1, strlen(str), logfile);
	//printf("%s", str);
	fclose(result);

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
	int port = 80;
	bool quitServer = FALSE;
	bool quitConnection;
	bool persistentConnection = TRUE;
	char str[128];
	char recvbuf[1024];
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

	printf("Web FMU Server\n\n");
	
	ptr = argv[0];
	char *tmp = ptr;
	while (getNextDelimiter(&ptr, "/")) { tmp = ptr; }
	*tmp = 0;
	strcpy(loadPath, argv[0]);
	loadPathLen = strlen(loadPath);
	//printf("%s\n\n", loadPath);
	for (i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "-c"))
		{
			persistentConnection = FALSE;
			printf("set connection type = closed\n\n");
		}
		else if (!strcmp(argv[i], "-p"))
		{
			persistentConnection = TRUE;
			printf("set connection type = persistent\n\n");
		}
		else if (!strcmp(argv[i], "-port"))
		{
			sscanf(argv[i + 1], "%d", &port);
			printf("set port = %d\n", port);
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
	printf("Bind Socket with Port: %d\n", port);

	if (listen(acceptSocket, 10)<0)
	{
		printf("Listen failed!\n");
		close(acceptSocket);
		return 1;
	}

	printf("Server started ...\n");

	while (!quitServer)
	{
		printf("Wait for connection...\n\n");
		serverSocket = accept(acceptSocket, NULL, NULL);
		if (serverSocket<0)
		{
			printf("Connection failed!\n");
			close(acceptSocket);
			return 1;
		}
		printf("Connection accepted.\n\n");

		quitConnection = FALSE;
		while (!quitConnection || persistentConnection)
		{
			databuf = NULL;
			printf("receiving...\n\n");
			if ((numbytes = recv(serverSocket, recvbuf, 1024, 0)) < 1)
			{
				printf("receiving failed!\n");
				quitConnection = TRUE;
				break;
			}
			recvbuf[numbytes] = 0;
			printf("%d bytes received:\n%s\n\n", numbytes, recvbuf);

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
	
						if (!strcmp(token, "txt"))
						{
							sprintf(fmuName, "%s.%s", fmuName, token);
							sprintf(str, "data/%s", fmuName);
							strcpy(loadPath + loadPathLen, str);
							stat(loadPath, &fileStat);
							sprintf(databuf + strlen(databuf), "<tr><td><a href='load?/data/%s'>%s</a></td><td>%d</td><td>%s</td></tr>", fmuName, fmuName, fileStat.st_size,ctime(&fileStat.st_mtime));
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
				readFile(loadPath, &databuflen, &databuf, "r");
				if (strstr(str, "xml")) sprintf(header, http_protocol, "no-store", databuflen, "text/xml");
				else if (strstr(str, "txt")) sprintf(header, http_protocol, "no-store", databuflen, "text/plain");
				else if (strstr(str, "svg")) sprintf(header, http_protocol, "max-age=3600", databuflen, "image/svg+xml");
				else sprintf(header, http_protocol, "no-store", databuflen, "text/html");
			}
			else if (strstr(recvbuf, "POST /sim"))
			{
				strcpy(loadPath + loadPathLen, "log.txt");
				logfile = fopen(loadPath, "w");
				ptr = strstr(recvbuf, "projname");
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
						sprintf(resultFileName, "%s%s", token, ".txt");
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
				fclose(logfile);
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
			printf("%d Bytes sent.\n", numbytes);
			printf("Header:\n%s\n\n", header);

			free(sendbuf);
			free(databuf);
			if (!persistentConnection) quitConnection = TRUE;
		}
		close(serverSocket);
	}

	close(acceptSocket);

	return 0;
}