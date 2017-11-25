#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "php.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
PHP_FUNCTION(testArray);

char plot_begin[] = "<div id='myDiv'>plot<!-- Plotly chart will be drawn inside this DIV --></div>\
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
	PHP_FE(testArray, NULL)
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
	int i = 0;
	int j;
	char path[] = "/var/www/html/simulation/data/";
	char fmuPath[] = "/var/www/html/simulation/fmu/";
	char fmuFileName[] = "DevLib_PT2.so";
	char fmuFullFilePath[128];
	char str[128];
	//char xVal[10000];
	//char yVal[10000];
	FILE *result;

	zval *arrParam, *arrParamRefs, *arrParamNames, *arrVarRefs, *arrVarNames, **data;
	HashTable *arr_hash;
	HashPosition pointer;

	fmi2String instanceName = "PT1_FMU";
	fmi2String guid = "{8c4e810f-3df3-4a00-8276-176fa3c9f9e0}";
	fmi2String fmuResourcesLocation = NULL;
	fmi2Real tolerance = 0.000001;
	fmi2Real tStart = 0;
	fmi2Real tStop = 1;
	fmi2Real tComm = tStart;
	fmi2Real tCommStep = 0.001; // FMU Exports duch OMC verwenden nur den Euler --> dort gibt es keine interne Schrittweite --> es wird nur die Kommunikationsschrittweite verwendet
	fmi2Status status = fmi2OK;
	size_t noOfParam;
	fmi2Real *parameter;
	fmi2ValueReference *paramRefs;
	fmi2String *paramNames;
	size_t noOfVars;
	fmi2Real *var;
	fmi2ValueReference *varRefs;
	fmi2String *varNames;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "saaaaa", &name, &name_len, &arrParamRefs, &arrParam, &arrParamNames, &arrVarRefs, &arrVarNames) == FAILURE) {
		RETURN_NULL();
	}

	arr_hash = Z_ARRVAL_P(arrParamRefs);
	noOfParam = zend_hash_num_elements(arr_hash);
	paramRefs = malloc(noOfParam * sizeof(long));
	i = 0;
	for (zend_hash_internal_pointer_reset_ex(arr_hash, &pointer); zend_hash_get_current_data_ex(arr_hash, (void**)&data, &pointer) == SUCCESS; zend_hash_move_forward_ex(arr_hash, &pointer))
	{
		if (Z_TYPE_PP(data) == IS_STRING) {
			sscanf(Z_STRVAL_PP(data), "%u", &paramRefs[i]);
		}
		i++;
	}

	arr_hash = Z_ARRVAL_P(arrParam);
	noOfParam = zend_hash_num_elements(arr_hash);
	parameter = malloc(noOfParam * sizeof(double));
	i = 0;
	for (zend_hash_internal_pointer_reset_ex(arr_hash, &pointer); zend_hash_get_current_data_ex(arr_hash, (void**)&data, &pointer) == SUCCESS; zend_hash_move_forward_ex(arr_hash, &pointer))
	{
		if (Z_TYPE_PP(data) == IS_STRING) {
			sscanf(Z_STRVAL_PP(data), "%lf", &parameter[i]);
		}
		i++;
	}

	arr_hash = Z_ARRVAL_P(arrParamNames);
	noOfParam = zend_hash_num_elements(arr_hash);
	paramNames = malloc(noOfParam * sizeof(char*));
	i = 0;
	for (zend_hash_internal_pointer_reset_ex(arr_hash, &pointer); zend_hash_get_current_data_ex(arr_hash, (void**)&data, &pointer) == SUCCESS; zend_hash_move_forward_ex(arr_hash, &pointer))
	{
		if (Z_TYPE_PP(data) == IS_STRING) {
			paramNames[i] = malloc(strlen(Z_STRVAL_PP(data)) + 1);
			strcpy((char*)paramNames[i], Z_STRVAL_PP(data));
		}
		i++;
	}

	arr_hash = Z_ARRVAL_P(arrVarRefs);
	noOfVars = zend_hash_num_elements(arr_hash);
	varRefs = malloc(noOfVars * sizeof(long));
	var = malloc(noOfVars * sizeof(double));
	i = 0;
	for (zend_hash_internal_pointer_reset_ex(arr_hash, &pointer); zend_hash_get_current_data_ex(arr_hash, (void**)&data, &pointer) == SUCCESS; zend_hash_move_forward_ex(arr_hash, &pointer))
	{
		if (Z_TYPE_PP(data) == IS_STRING) {
			sscanf(Z_STRVAL_PP(data), "%u", &varRefs[i]);
		}
		i++;
	}

	arr_hash = Z_ARRVAL_P(arrVarNames);
	noOfVars = zend_hash_num_elements(arr_hash);
	varNames = malloc(noOfVars * sizeof(char*));
	i = 0;
	for (zend_hash_internal_pointer_reset_ex(arr_hash, &pointer); zend_hash_get_current_data_ex(arr_hash, (void**)&data, &pointer) == SUCCESS; zend_hash_move_forward_ex(arr_hash, &pointer))
	{
		if (Z_TYPE_PP(data) == IS_STRING) {
			varNames[i] = malloc(strlen(Z_STRVAL_PP(data)) + 1);
			strcpy((char*)varNames[i], Z_STRVAL_PP(data));
		}
		i++;
	}

	i = 0;

	sprintf(str, "%s%s%s", path, name, ".txt");
	result = fopen(str, "w");
	sprintf(str, "%s%s%s", path, "log", ".txt");
	logfile = fopen(str, "w");

	strcpy(str, "Simulate FMU.\n");
	fwrite(str, 1, strlen(str), logfile);

	sprintf(fmuFullFilePath, "%s%s", fmuPath, fmuFileName);
	if (initFMU(instanceName, guid, fmuResourcesLocation, fmuFullFilePath) != 0)
	{
		fclose(logfile);
		RETURN_TRUE;
	}

	/*
	strcpy(str, "Result File\n");
	fwrite(str, 1, strlen(str), result);
	strcpy(str, "Projektname: ");
	fwrite(str, 1, strlen(str), result);
	sprintf(str, "%s%c", name, '\n');
	fwrite(str, 1, strlen(str), result);

	sprintf(str, "%s%.3f%c", "Parameter T: ", tau, '\n');
	fwrite(str, 1, strlen(str), result);
	*/
	sprintf(str, "%s,%s", "index", "time");
	fwrite(str, 1, strlen(str), result);
	for (j = 0; j < noOfVars; j++)
	{
		sprintf(str, ",%s", varNames[j]);
		fwrite(str, 1, strlen(str), result);
	}
	fwrite("\n", 1, 1, result);

	//strcpy(xVal, "x:[");
	//strcpy(yVal, "y:[");

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
	sprintf(str, "%d,%0.3f", i, tComm);
	fwrite(str, 1, strlen(str), result);
	for (j = 0; j < noOfVars; j++)
	{
		sprintf(str, ",%0.3f", var[j]);
		fwrite(str, 1, strlen(str), result);
	}
	fwrite("\n", 1, 1, result);

	/*
	sprintf(str, "%0.3f,", tComm);
	strcat(xVal, str);
	sprintf(str, "%0.3f,", var[0]);
	strcat(yVal, str);
	*/
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
		sprintf(str, "%d,%0.3f", i, tComm);
		fwrite(str, 1, strlen(str), result);
		for (j = 0; j < noOfVars; j++)
		{
			sprintf(str, ",%0.3f", var[j]);
			fwrite(str, 1, strlen(str), result);
		}
		fwrite("\n", 1, 1, result);
		/*
		sprintf(str, "%0.3f,", tComm);
		strcat(xVal, str);
		sprintf(str, "%0.3f,", var[0]);
		strcat(yVal, str);
		*/
	}
	
	/*
	strcpy(xVal+strlen(xVal)-1, "],\n");
	strcpy(yVal+strlen(yVal)-1, "],\n");
	php_printf("%s", plot_begin);
	php_printf("%s", xVal);
	php_printf("%s", yVal);
	php_printf("%s", plot_end);
	*/

	php_printf("<table>");
	php_printf("<tr><td>%s</td><td>%s</td></tr>", "Projektname", name);
	for (i = 0; i < noOfParam; i++)
	{
		php_printf("<tr><td>%s</td><td>%0.3f</td></tr>", paramNames[i], parameter[i]);
	}
	php_printf("<tr><td>%s</td><td>%0.3f</td></tr>", "t_end", tComm);
	for (i = 0; i < noOfVars; i++)
	{
		php_printf("<tr><td>%s</td><td>%0.3f</td></tr>", varNames[i], var[i]);
	}
	php_printf("</table>");

	if (status == fmi2OK)
	{
		fmi2Terminate(fmuInstance);
		fmi2Reset(fmuInstance);
		fmi2FreeInstance(fmuInstance);
		free(parameter);
		free(paramRefs);
	}

	strcpy(str, "Simulation succesfully terminated.\n");
	fwrite(str, 1, strlen(str), logfile);
	fclose(result);
	fclose(logfile);

	RETURN_TRUE;
}

PHP_FUNCTION(testArray)
{
	zval *arr, *arr2, **data;
	HashTable *arr_hash;
	HashPosition pointer;
	int array_count;
	double *d;
	int i;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "aa", &arr, &arr2) == FAILURE) {
		RETURN_NULL();
	}
	arr_hash = Z_ARRVAL_P(arr);
	array_count = zend_hash_num_elements(arr_hash);
	php_printf("The array passed contains %d elements<br>", array_count);

	for (zend_hash_internal_pointer_reset_ex(arr_hash, &pointer); zend_hash_get_current_data_ex(arr_hash, (void**)&data, &pointer) == SUCCESS; zend_hash_move_forward_ex(arr_hash, &pointer))
	{
		if (Z_TYPE_PP(data) == IS_STRING) {
			php_printf("%s<br>", Z_STRVAL_PP(data));
		}
	}

	arr_hash = Z_ARRVAL_P(arr2);
	array_count = zend_hash_num_elements(arr_hash);
	php_printf("The array passed contains %d elements<br>", array_count);
	d = malloc(array_count * sizeof(double));
	i = 0;
	for (zend_hash_internal_pointer_reset_ex(arr_hash, &pointer); zend_hash_get_current_data_ex(arr_hash, (void**)&data, &pointer) == SUCCESS; zend_hash_move_forward_ex(arr_hash, &pointer))
	{
		if (Z_TYPE_PP(data) == IS_STRING) {
			php_printf("%s<br>", Z_STRVAL_PP(data));
			sscanf(Z_STRVAL_PP(data), "%lf", &d[i]);
			php_printf("%0.3f<br>", d[i]);
		}
		i++;
	}

	RETURN_TRUE;
}
