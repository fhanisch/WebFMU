//gcc -std=c11 -o server server.c -ldl
/// @date: 01.12.2017

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dlfcn.h>
#include <dirent.h>
#include "fmi2FunctionTypes.h"

#define FALSE 0

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
const char http_protocol[] = "HTTP/1.1 200 OK\r\nServer: WebFMU Server\r\n";

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
			if (**src == delimiter[i]) return delimiter[i];
		}
		(*src)++;
	}

	return -1;
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

int simulate(char *resultFullFilePath, Variables *variables, char *htmlbuf)
{
	int i = 0;
	int j;
	char str[128];
	FILE *result;
	fmi2Real tolerance = 0.000001;
	fmi2Real tStart = 0;
	fmi2Real tStop = 1;
	fmi2Real tComm = tStart;
	fmi2Real tCommStep = 0.001; // FMU Exports duch OMC verwenden nur den Euler --> dort gibt es keine interne Schrittweite --> es wird nur die Kommunikationsschrittweite verwendet
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
	
	while (tComm < tStop && status == fmi2OK)
	{
		status = fmi2DoStep(fmuInstance, tComm, tCommStep, fmi2True);
		if (status != fmi2OK)
		{
			sprintf(str, "Error doStep!\n");
			fwrite(str, 1, strlen(str), logfile);
		}
		tComm += tCommStep;
		i++;
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
	}
	
	char *ptr = resultFullFilePath;
	char token[32];
	getNextDelimiter(&ptr, "/");
	findNextToken(&ptr, token);
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

int genSendBufFromFile(char *filename, int *bufsize, char **buf, char *readMode)
{
	FILE *file;
	int filesize;

	file = fopen(filename, readMode);
	fseek(file, 0, SEEK_END);
	filesize = ftell(file);
	rewind(file);
	*bufsize = strlen(header) + filesize;
	*buf = (char*)malloc(*bufsize + 1);
	strcpy(*buf, header);
	fread(*buf + strlen(header), filesize, 1, file);
	(*buf)[*bufsize] = 0;
	fclose(file);

	return 0;
}

int main(int argc, char *argv[])
{
	int acceptSocket, serverSocket;
	struct sockaddr_in addr;
	int port = 80;
	bool quit = FALSE;
	char str[128];
	char recvbuf[1024];
	char *sendbuf;
	int sendbuflen;
	int numbytes;
	int i;
	char fmuPath[] = "fmu/";
	char fmuFileName[64];
	char fmuFullFilePath[128];
	char resultFilePath[] = "data/";
	char resultFileName[64];
	char resultFullFilePath[128];
	char linkModelParam[64];
	Variables variables;
	char token[256];
	fmi2String instanceName = "WebFMU";
	fmi2String guid = "{8c4e810f-3df3-4a00-8276-176fa3c9f9e0}";
	fmi2String fmuResourcesLocation = NULL;
	char *ptr;

	printf("Web FMU Server\n\n");

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

	while (!quit)
	{
		sendbuf = NULL;
		sprintf(header, "%s\r\n", http_protocol);
		//printf("Wait for connection...\n\n");
		serverSocket = accept(acceptSocket, NULL, NULL);
		if (serverSocket<0)
		{
			printf("Connection failed!\n");
			close(acceptSocket);
			return 1;
		}
		//printf("Connection accepted.\n\n");
		
		if ((numbytes = recv(serverSocket, recvbuf, 1024, 0)) < 1)
		{
			printf("receiving failed!\n");
			close(serverSocket);
			continue;
		}
		recvbuf[numbytes] = 0;
		printf("%i bytes received:\n%s\n\n", numbytes, recvbuf);

		if (strstr(recvbuf, "GET / HTTP/1.1"))
		{
			genSendBufFromFile("sites/index.html", &sendbuflen, &sendbuf, "r");
		}
		else if (strstr(recvbuf, "GET /style.css"))
		{
			genSendBufFromFile("sites/style.css", &sendbuflen, &sendbuf, "r");
		}
		else if (strstr(recvbuf, "GET /favicon"))
		{
			sprintf(header, "%s%s\r\n", http_protocol, "Content-Type: image/apng\r\n");
			genSendBufFromFile("FMU_32x32.png", &sendbuflen, &sendbuf, "rb");
		}
		else if (strstr(recvbuf, "GET /logo"))
		{
			sprintf(header, "%s%s\r\n", http_protocol, "Content-Type: image/svg+xml\r\n");
			genSendBufFromFile("FMU.svg", &sendbuflen, &sendbuf, "r");
		}
		else if (strstr(recvbuf, "GET /modelparameter"))
		{
			sprintf(str, "fmu/%s.xml", linkModelParam);
			genSendBufFromFile(str, &sendbuflen, &sendbuf, "r");
		}
		else if (strstr(recvbuf, "GET /modelmenu"))
		{
			DIR *dp;
			struct dirent *ep;
			char fmuName[128];
			char token[128];
			char *ptr;

			sendbuf = malloc(1024);
			sprintf(sendbuf, "%s<select id = 'modelSelection' onchange = 'changeSelection()' style = 'width: 50%'>", header);

			dp = opendir("fmu");
			if (dp != NULL)
			{
				while (ep = readdir(dp))
				{
					ptr = ep->d_name;
					findNextToken(&ptr, fmuName);
					findNextToken(&ptr, token);
					if (!strcmp(token, "xml")) sprintf(sendbuf + strlen(sendbuf), "<option>%s</option>", fmuName);
				}
				(void)closedir(dp);
			}
			else
				printf("Couldn't open the directory!\n");

			sprintf(sendbuf + strlen(sendbuf), "</select>");
			sendbuflen = strlen(sendbuf);
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
			sprintf(str+strlen(str), "%s.", token);
			findNextToken(&ptr, token);
			sprintf(str+ strlen(str), "%s", token);
			if (getNextDelimiter(&ptr, "&/") == '&')
			{
				findNextToken(&ptr, linkModelParam);
			}
			genSendBufFromFile(str, &sendbuflen, &sendbuf, "r");
		}
		else if (strstr(recvbuf, "POST /sim"))
		{
			logfile = fopen("log.txt", "w");
			ptr = strstr(recvbuf, "projname");
			//printf("%s\n\n", ptr);
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
					ptr++;
					sscanf(ptr, "%d", &variables.paramRefs[i]);
				}
				else if (!strcmp(token, "pval"))
				{
					ptr++;
					sscanf(ptr, "%d", &i);
					getNextDelimiter(&ptr, "=");
					ptr++;
					sscanf(ptr, "%lf", &variables.parameter[i]);
				}
				else if (!strcmp(token, "vname"))
				{
					ptr++;
					sscanf(ptr, "%d", &i);
					findNextTokenLimited(&ptr, token,'&');
					variables.varNames[i] = malloc(strlen(token));
					strcpy((char*)variables.varNames[i], token);
					variables.noOfVars++;
				}
				else if (!strcmp(token, "vref"))
				{
					ptr++;
					sscanf(ptr, "%d", &i);
					getNextDelimiter(&ptr, "=");
					ptr++;
					sscanf(ptr, "%d", &variables.varRefs[i]);
				}
			}
			sprintf(fmuFullFilePath, "%s%s", fmuPath, fmuFileName);
			sprintf(resultFullFilePath, "%s%s", resultFilePath, resultFileName);
			sendbuf = malloc(5000);
			strcpy(sendbuf, header);
			if ( !initFMU(instanceName, guid, fmuResourcesLocation, fmuFullFilePath) )
			{
				if (!simulate(resultFullFilePath, &variables, sendbuf + strlen(header)))
				{
					sprintf(sendbuf + strlen(sendbuf), "<p>FMU %s instanciated.</p><p>Result File %s created.</p>"
						"<p>Simulation succesfully terminated.</p><div id='plot'></div>", fmuFileName, resultFileName);
				}
				else
				{
					strcat(sendbuf, "<p>Simulation Error!</p><p>Simulation failed!</p>");
				}
				//fmi2Terminate(fmuInstance);
				//fmi2Reset(fmuInstance);
				fmi2FreeInstance(fmuInstance);
				dlclose (handle);
			}
			else
			{
				sprintf(sendbuf + strlen(sendbuf), "<p>FMU %s could not be instanciated!</p><p>Simulation failed!</p>", fmuFileName);
			}
			sendbuflen = strlen(sendbuf);
			fclose(logfile);
		}
		else
		{
			sendbuf = malloc(128);
			sprintf(sendbuf, "%s<p style='font-size:100px;text-align:center'>What!</p>", header);
			sendbuflen = strlen(sendbuf);
		}
		if (!sendbuf) continue;
		if ((numbytes = send(serverSocket, sendbuf, sendbuflen, 0)) < 1)
		{
			printf("Senden failed!\n");
		}
		printf("%i Bytes sent.\n\n", numbytes);

		close(serverSocket);
		free(sendbuf);
	}

	close(acceptSocket);

	return 0;
}