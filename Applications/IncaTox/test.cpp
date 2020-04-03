
#define MOBIUS_TIMESTEP_VERBOSITY 0
#define MOBIUS_TEST_FOR_NAN 0
#define MOBIUS_EQUATION_PROFILING 1
#define MOBIUS_PRINT_TIMING_INFO 1
#define MOBIUS_INDEX_BOUNDS_TESTS 1

#include "../../mobius.h"

#include "../../Modules/Persist_Manning.h"
#include "../../Modules/INCA-Sed.h"
#include "../../Modules/SoilTemperature.h"
#include "../../Modules/WaterTemperature.h"
#include "../../Modules/INCA-Tox-C.h"
#include "../../Modules/INCA-Tox.h"

int main()
{
    const char *InputFile = "testinputs.dat";
	const char *ParameterFile = "testparameters.dat";
	
	mobius_model *Model = BeginModelDefinition("INCA-Tox");
	
	AddPersistModel(Model);
	AddINCASedModel(Model);
	AddSoilTemperatureModel(Model);
	AddWaterTemperatureModel(Model);
	AddIncaToxDOCModule(Model);
	AddIncaToxModule(Model);
	
	ReadInputDependenciesFromFile(Model, InputFile);
	
	EndModelDefinition(Model);
	
	mobius_data_set *DataSet = GenerateDataSet(Model);
    
	ReadParametersFromFile(DataSet, ParameterFile);
	
    
	ReadInputsFromFile(DataSet, InputFile);
	
	//WriteParametersToFile(DataSet, "newparams.dat");
	
	PrintResultStructure(Model);
	//PrintParameterStorageStructure(DataSet);
	//PrintInputStorageStructure(DataSet);
	
	RunModel(DataSet);
}


