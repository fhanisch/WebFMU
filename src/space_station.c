// SpaceStation - FMU
// gcc -std=c11 -shared -Wall -o space_station_fmu.so space_station.c -lm

#include <stdlib.h>
#include <math.h>

#ifdef WINDOWS
	#define EXPORT __declspec(dllexport)
#else
	#define EXPORT
#endif

typedef void*           fmi2Component;
typedef void*           fmi2ComponentEnvironment;
typedef void*           fmi2FMUstate;
typedef unsigned int    fmi2ValueReference;
typedef double          fmi2Real;
typedef int             fmi2Integer;
typedef int             fmi2Boolean;
typedef char            fmi2Char;
typedef const fmi2Char* fmi2String;
typedef char            fmi2Byte;

typedef enum {
	fmi2OK,
	fmi2Warning,
	fmi2Discard,
	fmi2Error,
	fmi2Fatal,
	fmi2Pending
} fmi2Status;

typedef enum {
	fmi2ModelExchange,
	fmi2CoSimulation
} fmi2Type;

typedef void(*fmi2CallbackLogger)        (fmi2ComponentEnvironment, fmi2String, fmi2Status, fmi2String, fmi2String, ...);
typedef void*(*fmi2CallbackAllocateMemory)(unsigned int, unsigned int);
typedef void(*fmi2CallbackFreeMemory)    (void*);
typedef void(*fmi2StepFinished)          (fmi2ComponentEnvironment, fmi2Status);

typedef struct {
	const fmi2CallbackLogger         logger;
	const fmi2CallbackAllocateMemory allocateMemory;
	const fmi2CallbackFreeMemory     freeMemory;
	const fmi2StepFinished           stepFinished;
	const fmi2ComponentEnvironment   componentEnvironment;
} fmi2CallbackFunctions;

typedef struct {
	double earth_m;
	double station_m;
	double earth_x[3];
	double station_x[3];
	double earth_der_x[3];
	double station_der_x[3];
	double earth_v[3];
	double station_v[3];
	double earth_der_v[3];
	double station_der_v[3];
	double f[3];
} FMU_Component;

char version[] = "FMU 2.0 by Felix";
const double G = 6.67408e-11;
const fmi2CallbackFunctions *cb;

EXPORT char *fmi2GetVersion()
{
	return version;
}

EXPORT fmi2Component fmi2Instantiate(fmi2String instanceName, fmi2Type fmi2CoSimulation, fmi2String guid, fmi2String fmuResourcesLocation, const fmi2CallbackFunctions* callbacks, fmi2Boolean visible, fmi2Boolean logginhOn)
{
	cb = callbacks;
	return malloc(sizeof(FMU_Component));
}

EXPORT fmi2Status fmi2SetupExperiment(FMU_Component *c, fmi2Boolean toleranceDefined, fmi2Real tolerance, fmi2Real tStart, fmi2Boolean tStopDefined, fmi2Real tStop)
{
	//init earth
	c->earth_m = 5.976e24;
	c->earth_x[0] = 0;
	c->earth_x[1] = 0;
	c->earth_x[2] = 0;
	c->earth_v[0] = 0;
	c->earth_v[1] = 0;
	c->earth_v[2] = 0;

	//init space station
	c->station_m = 500e3;
	c->station_x[0] = 0;
	c->station_x[1] = 6378000 + 400000;
	c->station_x[2] = 0;
	c->station_v[0] = 7650;
	c->station_v[1] = 0;
	c->station_v[2] = 0;

	return fmi2OK;
}

EXPORT fmi2Status fmi2EnterInitializationMode(fmi2Component c)
{
	return fmi2OK;
}
EXPORT fmi2Status fmi2ExitInitializationMode(fmi2Component c)
{ 
	return fmi2OK;
}
EXPORT fmi2Status fmi2Terminate(fmi2Component c)
{ 
	return fmi2OK;
}
EXPORT fmi2Status fmi2Reset(fmi2Component c)
{
	return fmi2OK;
}

EXPORT void fmi2FreeInstance(fmi2Component c)
{ 
	cb->freeMemory(c);
}

EXPORT fmi2Status fmi2GetReal(FMU_Component *c, const fmi2ValueReference refs[], unsigned int n, fmi2Real vars[])
{ 
	for (unsigned int i=0; i < n; i++)
	{
		if (refs[i] == 0) vars[i] = c->station_x[0];
		else if (refs[i] == 1) vars[i] = c->station_x[1];
		else if (refs[i] == 2) vars[i] = c->earth_x[0];
		else if (refs[i] == 3) vars[i] = c->earth_x[1];
	}
	return fmi2OK;
}

EXPORT fmi2Status fmi2SetReal(fmi2Component c, const fmi2ValueReference refs[], unsigned int n, const fmi2Real vars[])
{ 
	return fmi2OK;
}

void gravitationalForce(double f[3], double r1[3],double r2[3], double m1, double m2)
{
	double r;

	r = sqrt(pow(r2[0]-r1[0],2) + pow(r2[1]-r1[1],2) + pow(r2[2]-r1[2],2));

	f[0] = G * m1 * m2 * (r2[0]-r1[0]) / pow(r, 3);
	f[1] = G * m1 * m2 * (r2[1]-r1[1]) / pow(r, 3);
	f[2] = G * m1 * m2 * (r2[2]-r1[2]) / pow(r, 3);
}

EXPORT fmi2Status fmi2DoStep(FMU_Component *c, fmi2Real t, fmi2Real h, fmi2Boolean noSetFMUStatePriorToCurrentPoint)
{
	gravitationalForce(c->f, c->earth_x, c->station_x, c->earth_m, c->station_m);

	// equations earth
	c->earth_der_x[0] = c->earth_v[0];
	c->earth_der_x[1] = c->earth_v[1];
	c->earth_der_x[2] = c->earth_v[2];

	c->earth_der_v[0] = 1.0 / c->earth_m * c->f[0];
	c->earth_der_v[1] = 1.0 / c->earth_m * c->f[1];
	c->earth_der_v[2] = 1.0 / c->earth_m * c->f[2];

	c->earth_x[0] += h * c->earth_der_x[0];
	c->earth_x[1] += h * c->earth_der_x[1];
	c->earth_x[2] += h * c->earth_der_x[2];

	c->earth_v[0] += h * c->earth_der_v[0];
	c->earth_v[1] += h * c->earth_der_v[1];
	c->earth_v[2] += h * c->earth_der_v[2];

	// equations space station
	c->station_der_x[0] = c->station_v[0];
	c->station_der_x[1] = c->station_v[1];
	c->station_der_x[2] = c->station_v[2];

	c->station_der_v[0] = -1.0 / c->station_m * c->f[0];
	c->station_der_v[1] = -1.0 / c->station_m * c->f[1];
	c->station_der_v[2] = -1.0 / c->station_m * c->f[2];

	c->station_x[0] += h * c->station_der_x[0];
	c->station_x[1] += h * c->station_der_x[1];
	c->station_x[2] += h * c->station_der_x[2];

	c->station_v[0] += h * c->station_der_v[0];
	c->station_v[1] += h * c->station_der_v[1];
	c->station_v[2] += h * c->station_der_v[2];

	return fmi2OK;
}