#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "php.h"
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include "fmi2FunctionTypes.h"

#define PHP_FMU_EXTENSION_VERSION "1.0"
#define PHP_FMU_EXTENSION_EXTNAME "fmu_extension"

extern zend_module_entry fmu_extension_module_entry;
#define phpext_fmu_extension_ptr &fmu_extension_module_entry

// declaration of a custom my_function()
PHP_FUNCTION(my_function);
PHP_FUNCTION(hello_long);
PHP_FUNCTION(hello_greetme);
PHP_FUNCTION(simulate);

char plot_begin[] = "<div id='myDiv'><!-- Plotly chart will be drawn inside this DIV --></div>\
				<script>\
				var trace1 = {";
char plot_end[] = "mode: 'lines',marker: {color: 'rgb(219, 64, 82)',size : 12}};\
				var data = [trace1];\
				var layout = {\
				height: 500,\
				xaxis: {title: 'Time [s]'}};\
				Plotly.newPlot('myDiv', data, layout);\
				</script>";

// list of custom PHP functions provided by this extension
// set {NULL, NULL, NULL} as the last record to mark the end of list
static zend_function_entry my_functions[] = {
    PHP_FE(my_function, NULL)
    PHP_FE(hello_long, NULL)
    PHP_FE(hello_greetme, NULL)
	PHP_FE(simulate, NULL)
    {NULL, NULL, NULL}
};

// the following code creates an entry for the module and registers it with Zend.
zend_module_entry fmu_extension_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
    STANDARD_MODULE_HEADER,
#endif
    PHP_FMU_EXTENSION_EXTNAME,
    my_functions,
    NULL, // name of the MINIT function or NULL if not applicable
    NULL, // name of the MSHUTDOWN function or NULL if not applicable
    NULL, // name of the RINIT function or NULL if not applicable
    NULL, // name of the RSHUTDOWN function or NULL if not applicable
    NULL, // name of the MINFO function or NULL if not applicable
#if ZEND_MODULE_API_NO >= 20010901
    PHP_FMU_EXTENSION_VERSION,
#endif
    STANDARD_MODULE_PROPERTIES
};

ZEND_GET_MODULE(fmu_extension)

//my code
void *handle;
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
FILE *logfile;
int isInitFMU = 0;

#define FMI_GET_FUNC_ADDR( fun )						\
			fun = (fun##TYPE*)dlsym(handle, #fun);		\
			if (fun == NULL)							\
			{											\
				sprintf(str,"Load %s failed!\n",#fun);	\
				fwrite(str, 1, strlen(str), logfile);	\
				return -1;								\
			}											\

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
	sprintf(str,"Logger: %s %s %s\n", b, d, e);
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

	sprintf(str,"FMI-Version: %s\n", fmi2GetVersion());
	fwrite(str, 1, strlen(str), logfile);

	sprintf(str, "GUID: %s\n", guid);
	fwrite(str, 1, strlen(str), logfile);
	fmuInstance = fmi2Instantiate(instanceName, fmi2CoSimulation, guid, fmuResourcesLocation, &callbacks, fmi2False, fmi2False);
	if (fmuInstance == NULL)
	{
		strcpy(str,"Get fmu instance failed!\n");
		fwrite(str, 1, strlen(str), logfile);
		return -1;
	}
	strcpy(str,"FMU instanceiated.\n");
	fwrite(str, 1, strlen(str), logfile);

	return 0;
}

// implementation of a custom my_function()
PHP_FUNCTION(my_function)
{
    RETURN_STRING("This is my function<br>", 1);
}

PHP_FUNCTION(hello_long)
{
  RETURN_LONG(42);
}

PHP_FUNCTION(hello_greetme)
{
  char *name;
  int name_len;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &name, &name_len) == FAILURE) {
    RETURN_NULL();
  }

  php_printf("<h1>Hello %s</h1>", name);
  php_printf("<a href=\"Schlauchreifenkleben.html\">Schlauchreifen kleben</a>");

  RETURN_TRUE;
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

PHP_FUNCTION(simulate)
{
	char *name;
	int name_len;
	double tau;
	int i = 0;
	char path[] = "/var/www/html/simulation/data/";
	char fmuPath[] = "/var/www/html/simulation/fmu/";
	char fmuFileName[] = "DevLib_PT2.so";
	char fmuFullFilePath[128];
	char str[128];
	char xVal[10000];
	char yVal[10000];
	FILE *result;

	fmi2String instanceName = "PT1_FMU";
	fmi2String guid = "{8c4e810f-3df3-4a00-8276-176fa3c9f9e0}";
	fmi2String fmuResourcesLocation = NULL;
	fmi2Real tolerance = 0.000001;
	fmi2Real tStart = 0;
	fmi2Real tStop = 1;
	fmi2Real tComm = tStart;
	fmi2Real tCommStep = 0.001; // FMU Exports duch OMC verwenden nur den Euler --> dort gibt es keine interne Schrittweite --> es wird nur die Kommunikationsschrittweite verwendet
	fmi2Status status = fmi2OK;
	fmi2Real parameter[] = { 0.01, 0.001, 1.0 };
	fmi2ValueReference paramRefs[] = { 4, 5, 6 };
	size_t noOfParam = 3;
	fmi2Real var[1];
	fmi2ValueReference varRefs[] = { 0 };
	size_t noOfVars = 1;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sd", &name, &name_len, &tau) == FAILURE) {
		RETURN_NULL();
	}
	parameter[0] = tau;
	
	sprintf(str, "%s%s%s", path, name, ".txt");
	result = fopen(str, "w");
	sprintf(str, "%s%s%s", path, "log", ".txt");
	logfile = fopen(str, "w");

	strcpy(str, "Simulate FMU.\n");
	fwrite(str, 1, strlen(str), logfile);

	if (!isInitFMU)
	{
		sprintf(fmuFullFilePath, "%s%s", fmuPath, fmuFileName);
		if (initFMU(instanceName, guid, fmuResourcesLocation, fmuFullFilePath) != 0)
		{
			fclose(logfile);
			RETURN_TRUE;
		}
	}
	isInitFMU = 1;

	strcpy(str, "Result File\n");
	fwrite(str, 1, strlen(str), result);
	strcpy(str, "Projektname: ");
	fwrite(str, 1, strlen(str), result);
	sprintf(str, "%s%c", name, '\n');
	fwrite(str, 1, strlen(str), result);

	sprintf(str, "%s%.3f%c", "Parameter T: ", tau, '\n');
	fwrite(str, 1, strlen(str), result);
	sprintf(str, "%s , %s , %s%c", "Index", "Time", "Value", '\n');
	fwrite(str, 1, strlen(str), result);

	php_printf("<table>");
	php_printf("<tr><th>Index</th> <th>Time</th>  <th>Value</th></tr>");

	strcpy(xVal, "x:[");
	strcpy(yVal, "y:[");

	if (fmi2SetupExperiment(fmuInstance, fmi2True, tolerance, tStart, fmi2False, tStop) != fmi2OK)
	{
		sprintf(str,"Experiment setup failed!\n");
		fwrite(str, 1, strlen(str), logfile);
		fclose(logfile);
		RETURN_TRUE;
	}

	if (fmi2SetReal(fmuInstance, paramRefs, noOfParam, parameter) != fmi2OK)
	{
		sprintf(str,"Set real failed!\n");
		fwrite(str, 1, strlen(str), logfile);
		fclose(logfile);
		RETURN_TRUE;
	}

	if (fmi2EnterInitializationMode(fmuInstance) != fmi2OK)
	{
		sprintf(str,"EnterInitializationMode failed!\n");
		fwrite(str, 1, strlen(str), logfile);
		fclose(logfile);
		RETURN_TRUE;
	}

	if (fmi2ExitInitializationMode(fmuInstance) != fmi2OK)
	{
		sprintf(str,"ExitInitializationMode failed!\n");
		fwrite(str, 1, strlen(str), logfile);
		fclose(logfile);
		RETURN_TRUE;
	}

	status = fmi2GetReal(fmuInstance, varRefs, noOfVars, var);
	if (status != fmi2OK)
	{
		sprintf(str, "Error getReal!\n");
		fwrite(str, 1, strlen(str), logfile);
	}
	sprintf(str, "%d , %0.3f , %0.3f%c", i, tComm, var[0], '\n');
	fwrite(str, 1, strlen(str), result);

	sprintf(str, "%0.3f,", tComm);
	strcat(xVal, str);
	sprintf(str, "%0.3f,", var[0]);
	strcat(yVal, str);

	php_printf("<tr>");
	php_printf("<td>%d</td>  <td>%0.3f</td>  <td>%0.3f</td>", i, tComm, var[0]);
	php_printf("</tr>");

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
		status = fmi2GetReal(fmuInstance, varRefs, noOfVars, var);
		if (status != fmi2OK)
		{
			sprintf(str, "Error getReal!\n");
			fwrite(str, 1, strlen(str), logfile);
		}
		sprintf(str, "%d , %0.3f , %0.3f%c", i,tComm,var[0],'\n');
		fwrite(str, 1, strlen(str), result);
		
		sprintf(str, "%0.3f,", tComm);
		strcat(xVal, str);
		sprintf(str, "%0.3f,", var[0]);
		strcat(yVal, str);

		php_printf("<tr>");
		php_printf("<td>%d</td>  <td>%0.3f</td>  <td>%0.3f</td>", i, tComm, var[0]);
		php_printf("</tr>");
	}
	php_printf("</table>");

	strcpy(xVal+strlen(xVal)-1, "],\n");
	strcpy(yVal+strlen(yVal)-1, "],\n");

	php_printf("%s", plot_begin);
	php_printf("%s", xVal);
	php_printf("%s", yVal);
	php_printf("%s", plot_end);
	fclose(result);
	fclose(logfile);

	if (status == fmi2OK)
	{
		fmi2Terminate(fmuInstance);
		fmi2Reset(fmuInstance);
		//fmi2FreeInstance(fmuInstance);
	}

	RETURN_TRUE;
}
