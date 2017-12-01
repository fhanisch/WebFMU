//gcc -std=c11 -o server server.c -ldl

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dlfcn.h>
#include "fmi2FunctionTypes.h"

#define FALSE 0
#define DELAY 100000
typedef int bool;

typedef struct
{
	int offset;
	int len;
	char name[256];
} Token;

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

const char header[] = "HTTP/1.1 200 OK\r\n\r\n";

const char modelmenu[] = "HTTP/1.1 200 OK\r\n\r\n"
"<select id='modelSelection' onchange='changeSelection()' style='width: 50%'>"
"<option>modelDescriptionPT2</option>"
"</select>";

const char modelinfo[] = "HTTP/1.1 200 OK\r\n\r\n"
"<p id = 'formel'>"
"\\begin{equation}"
"\\ddot{ x } = \\frac{ 1 }{c_2} (k - c_1\\dot{ x }-x)"
"\\end{equation}"
"</p>";

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

#define FMI_GET_FUNC_ADDR( fun )						\
			fun = (fun##TYPE*)dlsym(handle, #fun);		\
			if (fun == NULL)							\
			{											\
				sprintf(str,"Load %s failed!\n",#fun);	\
				fwrite(str, 1, strlen(str), logfile);	\
				return -1;								\
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
	printf("%s", str);

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

	sprintf(htmlbuf, "%s", "<table>");
	sprintf(htmlbuf+strlen(htmlbuf), "<tr><td>%s</td><td>%s</td></tr>", "Projektname", resultFullFilePath);
	for (i = 0; i < variables->noOfParam; i++)
	{
		sprintf(htmlbuf + strlen(htmlbuf), "<tr><td>%s</td><td>%0.3f</td></tr>", variables->paramNames[i], variables->parameter[i]);
	}
	sprintf(htmlbuf + strlen(htmlbuf), "<tr><td>%s</td><td>%0.3f</td></tr>", "t_end", tComm);
	for (i = 0; i < variables->noOfVars; i++)
	{
		sprintf(htmlbuf + strlen(htmlbuf), "<tr><td>%s</td><td>%0.3f</td></tr>", variables->varNames[i], variables->var[i]);
	}
	sprintf(htmlbuf + strlen(htmlbuf), "</table>");

	if (status == fmi2OK)
	{
		fmi2Terminate(fmuInstance);
		fmi2Reset(fmuInstance);
		fmi2FreeInstance(fmuInstance);
	}

	strcpy(str, "Simulation succesfully terminated.\n");
	fwrite(str, 1, strlen(str), logfile);
	printf("%s", str);
	fclose(result);

	return 0;
}

int readFile(char *filename, char **data)
{
	FILE *file;
	int filesize;

	file = fopen(filename, "r");
	fseek(file, 0, SEEK_END);
	filesize = ftell(file);
	printf("filesize: %d\n", filesize);
	rewind(file);
	*data = (char*)malloc(filesize + 1);
	memset(*data, 0, filesize + 1);
	fread(*data, filesize, 1, file);
	fclose(file);

	return 0;
}

char getNextDelimiter(char *src, int *ptr, char *delimiter)
{
	*ptr = 0;
	while (src[*ptr] != 0)
	{
		for (uint32_t i = 0; i < strlen(delimiter); i++)
		{
			if (src[*ptr] == delimiter[i]) return delimiter[i];
		}
		(*ptr)++;
	}

	return -1;
}

int findNextToken(char *src, int ptr, Token *tk)
{
	int offset;
	int len = 0;

	while (src[ptr] < 65)
	{
		if (src[ptr] == 0) return -1;
		ptr++;
	}
	offset = ptr;
	while ((src[ptr] >= 48 && src[ptr] <= 57) || src[ptr] >= 65)
	{
		if (src[ptr] == 0) return -1;
		len++;
		ptr++;
	}
	memcpy(tk->name, src + offset, len);
	tk->name[len] = 0;
	tk->offset = offset;
	tk->len = len;
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
	char indexbuf[20000];
	char stylebuf[20000];
	char modelDescriptionBuf[5000];
	char simbuf[5000];
	char logbuf[5000];
	int numbytes;
	int i;
	char *data;
	char fmuPath[] = "fmu/";
	char fmuFileName[64];
	char fmuFullFilePath[128];
	char resultFilePath[] = "data/";
	char resultFileName[64];
	char resultFullFilePath[128];
	Variables variables;
	char htmlbuf[5000];

	fmi2String instanceName = "WebFMU";
	fmi2String guid = "{8c4e810f-3df3-4a00-8276-176fa3c9f9e0}";
	fmi2String fmuResourcesLocation = NULL;
	Token token;
	char *postParamOffset;

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

	readFile("index.html", &data);
	sprintf(indexbuf, "%s%s", header, data);
	indexbuf[strlen(header) + strlen(data)] = 0;

	readFile("style.css", &data);
	sprintf(stylebuf, "%s%s", header, data);
	stylebuf[strlen(header) + strlen(data)] = 0;

	readFile("modelDescriptionPT2.xml", &data);
	sprintf(modelDescriptionBuf, "%s%s", header, data);
	modelDescriptionBuf[strlen(header) + strlen(data)] = 0;

	readFile("log.txt", &data);
	sprintf(logbuf, "%s%s", header, data);
	logbuf[strlen(header) + strlen(data)] = 0;

	while (!quit)
	{
		//printf("Wait for connection...\n\n");
		serverSocket = accept(acceptSocket, NULL, NULL);
		if (serverSocket<0)
		{
			printf("Connection failed!\n");
			close(acceptSocket);
			return 1;
		}
		//printf("Connection accepted.\n\n");
		
		if ((numbytes = recv(serverSocket, recvbuf, 1024, 0)) < 0)
		{
			printf("receiving failed!\n");
			close(serverSocket);
			continue;
		}
		recvbuf[numbytes] = 0;
		//printf("%i bytes received:\n%s\n\n", numbytes, recvbuf);

		if (strstr(recvbuf, "GET / HTTP/1.1"))
		{
			if ((numbytes = send(serverSocket, indexbuf, strlen(indexbuf), 0)) < 0)
			{
				printf("Senden failed!\n");
				close(serverSocket);
				continue;
			}
			//printf("%i Bytes sent.\n\n", numbytes);
		}
		else if (strstr(recvbuf, "GET /style.css"))
		{
			if ((numbytes = send(serverSocket, stylebuf, strlen(stylebuf), 0)) < 0)
			{
				printf("Senden failed!\n");
				close(serverSocket);
				continue;
			}
			//printf("%i Bytes sent.\n\n", numbytes);
		}
		else if (strstr(recvbuf, "GET /modelmenu"))
		{
			if ((numbytes = send(serverSocket, modelmenu, strlen(modelmenu), 0)) < 0)
			{
				printf("Senden failed!\n");
				close(serverSocket);
				continue;
			}
			//printf("%i Bytes sent.\n\n", numbytes);
		}
		else if (strstr(recvbuf, "GET /simulation/fmu/modelInfoPT2.txt"))
		{
			if ((numbytes = send(serverSocket, modelinfo, strlen(modelinfo), 0)) < 0)
			{
				printf("Senden failed!\n");
				close(serverSocket);
				continue;
			}
			//printf("%i Bytes sent.\n\n", numbytes);
		}

		else if (strstr(recvbuf, "GET /simulation/fmu/modelDescriptionPT2.xml"))
		{
			if ((numbytes = send(serverSocket, modelDescriptionBuf, strlen(modelDescriptionBuf), 0)) < 0)
			{
				printf("Senden failed!\n");
				close(serverSocket);
				continue;
			}
			//printf("%i Bytes sent.\n\n", numbytes);
		}

		else if (strstr(recvbuf, "GET /log"))
		{
			if ((numbytes = send(serverSocket, logbuf, strlen(logbuf), 0)) < 0)
			{
				printf("Senden failed!\n");
				close(serverSocket);
				continue;
			}
			//printf("%i Bytes sent.\n\n", numbytes);
		}

		else if (strstr(recvbuf, "POST /sim"))
		{
			logfile = fopen("log.txt", "w");
			postParamOffset = strstr(recvbuf, "projname");
			token.offset = (int)(postParamOffset - recvbuf);
			token.len = 0;
			variables.noOfParam = 0;
			variables.noOfVars = 0;
			while (!findNextToken(recvbuf, token.offset + token.len, &token))
			{
				if (!strcmp(token.name, "fmuname"))
				{
					findNextToken(recvbuf, token.offset + token.len, &token);
					sprintf(fmuFileName, "%s%s", token.name, ".so");
				}
				else if (!strcmp(token.name, "projname"))
				{

					findNextToken(recvbuf, token.offset + token.len, &token);
					sprintf(resultFileName, "%s%s", token.name, ".txt");
				}
				else if (strstr(token.name, "pname"))
				{
					getNextDelimiter(token.name, &i, "0123456789");
					sscanf(token.name+i, "%d", &i);
					printf("index: %d\n\t%s: ", i, token.name);
					findNextToken(recvbuf, token.offset + token.len, &token);
					variables.paramNames[i] = malloc(strlen(token.name));
					strcpy((char*)variables.paramNames[i], token.name);
					printf("%s\n", variables.paramNames[i]);
					variables.noOfParam++;
				}
				else if (strstr(token.name, "pref"))
				{
					getNextDelimiter(token.name, &i, "0123456789");
					sscanf(token.name + i, "%d", &i);
					getNextDelimiter(recvbuf + token.offset + token.len, &i, "=");
					sscanf(recvbuf + token.offset + token.len + i + 1, "%d", &variables.paramRefs[i]);
					printf("\t%s = %d\n", token.name, variables.paramRefs[i]);
				}
				else if (strstr(token.name, "pval"))
				{
					getNextDelimiter(token.name, &i, "0123456789");
					sscanf(token.name + i, "%d", &i);
					getNextDelimiter(recvbuf + token.offset + token.len, &i, "=");
					sscanf(recvbuf + token.offset + token.len + i + 1, "%lf", &variables.parameter[i]);
					printf("\t%s = %0.3lf\n", token.name, variables.parameter[i]);
				}
				else if (strstr(token.name, "vname"))
				{
					getNextDelimiter(token.name, &i, "0123456789");
					sscanf(token.name + i, "%d", &i);
					printf("index: %d\n\t%s: ", i, token.name);
					findNextToken(recvbuf, token.offset + token.len, &token);
					variables.varNames[i] = malloc(strlen(token.name));
					strcpy((char*)variables.varNames[i], token.name);
					printf("%s\n", variables.varNames[i]);
					variables.noOfVars++;
				}
				else if (strstr(token.name, "vref"))
				{
					getNextDelimiter(token.name, &i, "0123456789");
					sscanf(token.name + i, "%d", &i);
					getNextDelimiter(recvbuf + token.offset + token.len, &i, "=");
					sscanf(recvbuf + token.offset + token.len + i + 1, "%d", &variables.varRefs[i]);
					printf("\t%s = %d\n", token.name, variables.varRefs[i]);
				}
			}
			sprintf(fmuFullFilePath, "%s%s", fmuPath, fmuFileName);
			sprintf(resultFullFilePath, "%s%s", resultFilePath, resultFileName);
			if (initFMU(instanceName, guid, fmuResourcesLocation, fmuFullFilePath) != 0)
			{
				fclose(logfile);
				continue;
			}
			simulate(resultFullFilePath, &variables, htmlbuf);
			fclose(logfile);
			sprintf(simbuf, "%s%s<p>FMU %s instanciated.</p><p>ResultFilePath: %s</p>"
				"<p>Simulation succesfully terminated.</p>", header, htmlbuf, fmuFullFilePath, resultFullFilePath);
			if ((numbytes = send(serverSocket, simbuf, strlen(simbuf), 0)) < 0)
			{
				printf("Senden failed!\n");
				close(serverSocket);
				continue;
			}
			//printf("%i Bytes sent.\n\n", numbytes);
		}
			
		//for (int i = 0; i<DELAY; i++);
		close(serverSocket);
	}

	close(acceptSocket);

	return 0;
}