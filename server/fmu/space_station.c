// SpaceStation - FMU
// gcc -shared -Wall -o space_station_fmu.so space_station.c

#include <math.h>

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

char version[] = "FMU 2.0 by Felix";
const double G = 6.67408e-11;
double m;
double x[3];
double der_x[3];
double v[3];
double der_v[3];
double f[3];

char *fmi2GetVersion()
{
	return version;
}

fmi2Component fmi2InstantiateTYPE(fmi2String instanceName, fmi2Type fmi2CoSimulation, fmi2String guid, fmi2String fmuResourcesLocation, const fmi2CallbackFunctions* callbacks, fmi2Boolean visible, fmi2Boolean logginhOn)
{

	return (fmi2Component)1;
}

fmi2Status fmi2SetupExperiment(fmi2Component c, fmi2Boolean toleranceDefined, fmi2Real tolerance, fmi2Real tStart, fmi2Boolean tStopDefined, fmi2Real tStop)
{
	m = 500e3;
	x[0] = 0;
	x[1] = 6378000 + 400000;
	x[2] = 0;
	v[0] = 7650;
	v[1] = 0;
	v[2] = 0;

	return fmi2OK;
}

void gravitationalForce(double f[3], double x[3], double m1, double m2)
{
	double r;

	r = sqrt(pow(x[0],2) + pow(x[1],2) + pow(x[2],2));

	f[0] = G * m1 * m2 * x[0] / pow(r, 3);
	f[1] = G * m1 * m2 * x[1] / pow(r, 3);
	f[2] = G * m1 * m2 * x[2] / pow(r, 3);
}

fmi2Status fmi2DoStep(fmi2Component c, fmi2Real t, fmi2Real h, fmi2Boolean noSetFMUStatePriorToCurrentPoint)
{
	gravitationalForce(f, x, m, 5.976e24);

	der_x[0] = v[0];
	der_x[1] = v[1];
	der_x[2] = v[2];

	der_v[0] = -1.0 / m * f[0];
	der_v[1] = -1.0 / m * f[1];
	der_v[2] = -1.0 / m * f[2];

	x[0] += h * der_x[0];
	x[1] += h * der_x[1];
	x[2] += h * der_x[2];

	v[0] += h * der_v[0];
	v[1] += h * der_v[1];
	v[2] += h * der_v[2];

	return fmi2OK;
}