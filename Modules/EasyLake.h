

// This will be a very simple lake model for use along with cathcment models.
// The goal is to simulate flows, temperature and carbon cycling, possibly along with N and P processes.


// The water balance part of the model is conceptually similar to VEMALA
// A National-Scale Nutrient Loading Model for Finnish Watersheds - VEMALA, Inse Huttunen et. al. 2016, Environ Model Assess 21, 83-109

// Air-lake fluxes are based off of
// Air-Sea bulk transfer coefficients in diabatic conditions, Junsei Kondo, 1975, Boundary-Layer Meteorology 9(1), 91-112
// Implementation is informed by the one in GOTM ( https://github.com/gotm-model/code/blob/master/src/airsea/kondo.F90 )

//NOTE: This model is IN DEVELOPMENT





	
inline double
SaturationVaporPressure(double Temperature)
{
	//NOTE: Saturation vapor pressure routine based on 
	// P. R. Lowe, 1977, "An approximating polynomial for the computation of saturation vapor pressure, J. Appl. Meteor., 16, 100-103.

	//TODO: Could some of this be unified with code in PET.h ?
	
	// Takes temperature in celsius
	// Returns saturation vapor pressure in millibar=hectopascal.
	
	double a0 = 6.107799961;
	double a1 = 4.436518521e-1;
	double a2 = 1.428945805e-2;
	double a3 = 2.650648471e-4;
	double a4 = 3.031240396e-6;
	double a5 = 2.034080948e-8;
	double a6 = 6.136820929e-11;
	double t = Temperature;
	
	return (a0 + t*(a1 + t*(a2 + t*(a3 + t*(a4 + t*(a5 + t*a6))))));
}

static void
AddEasyLakePhysicalModule(mobius_model *Model)
{
	BeginModule(Model, "Easy-Lake physical", "_dev");
	
	auto Dimensionless  = RegisterUnit(Model);
	auto M              = RegisterUnit(Model, "m");
	auto M2             = RegisterUnit(Model, "m2");
	auto M3             = RegisterUnit(Model, "m3");
	auto MPerS          = RegisterUnit(Model, "m/s");
	auto M3PerS         = RegisterUnit(Model, "m3/s");
	auto MmPerDay       = RegisterUnit(Model, "mm/day");
	auto MPerM          = RegisterUnit(Model, "m/m");
	auto Degrees        = RegisterUnit(Model, "°");
	auto DegreesCelsius = RegisterUnit(Model, "°C");
	auto KgPerKg        = RegisterUnit(Model, "kg/kg");
	auto KgPerM3        = RegisterUnit(Model, "kg/m3");
	auto Percent        = RegisterUnit(Model, "%");
	auto Pascal         = RegisterUnit(Model, "Pa");
	auto HPa            = RegisterUnit(Model, "HPa");
	auto WPerM2         = RegisterUnit(Model, "W/m2");
	
	auto PhysParams = RegisterParameterGroup(Model, "Lake physical parameters");
	
	auto LakeSurfaceArea                = RegisterParameterDouble(Model, PhysParams, "Lake surface area", M2, 1e3, 0.0, 371e9);
	auto LakeLength                     = RegisterParameterDouble(Model, PhysParams, "Lake length", M, 300.0, 0.0, 1.03e6, "This parameter should be adjusted when calibrating lake outflow");
	auto LakeShoreSlope                 = RegisterParameterDouble(Model, PhysParams, "Lake shore slope", MPerM, 0.2, 0.0, 4.0, "This parameter should be adjusted when calibrating lake outflow. Slope is roughly 2*depth/width");
	auto WaterLevelAtWhichOutflowIsZero = RegisterParameterDouble(Model, PhysParams, "Water level at which outflow is 0", M, 10.0, 0.0, 1642.0);
	auto OutflowRatingCurveShape        = RegisterParameterDouble(Model, PhysParams, "Outflow rating curve shape", Dimensionless, 0.3, 0.0, 1.0, "0 if rating curve is linear, 1 if rating curve is a parabola. Values in between give linear interpolation between these types of curves.");
	auto OutflowRatingCurveMagnitude    = RegisterParameterDouble(Model, PhysParams, "Outflow rating curve magnitude", Dimensionless, 1.0, 0.01, 100.0, "Outflow is proportional to 10^(magnitude)");
	auto InitialWaterLevel              = RegisterParameterDouble(Model, PhysParams, "Initial water level", M, 10.0, 0.0, 1642.0);
	
	//TODO: We should make a flexible way for this to either be taken from (one or more) reach sections in a directly coupled model, OR be an input timeseries
	auto LakeInflow       = RegisterInput(Model, "Lake inflow", M3PerS);
	auto Precipitation    = RegisterInput(Model, "Precipitation", MmPerDay);
	auto AirTemperature   = RegisterInput(Model, "Air temperature", DegreesCelsius);
	auto WindSpeed        = RegisterInput(Model, "Wind speed at 10m", MPerS);
	auto RelativeHumidity = RegisterInput(Model, "Relative humidity", Percent);
	auto AirPressure      = RegisterInput(Model, "Air pressure", HPa);
	auto CloudCover       = RegisterInput(Model, "Cloud cover", Dimensionless);
	
	auto LakeSolver = RegisterSolver(Model, "Lake solver", 0.1, IncaDascru);
	
	auto LakeVolume        = RegisterEquationODE(Model, "Lake volume", M3);
	auto InitialLakeVolume = RegisterEquationInitialValue(Model, "Initial lake volume", M3);
	SetSolver(Model, LakeVolume, LakeSolver);
	SetInitialValue(Model, LakeVolume, InitialLakeVolume);
	
	auto WaterLevel = RegisterEquationODE(Model, "Water level", M);
	SetSolver(Model, WaterLevel, LakeSolver);
	SetInitialValue(Model, WaterLevel, InitialWaterLevel);
	
	auto DVDT        = RegisterEquation(Model, "Change in lake volume", M3PerS);
	SetSolver(Model, DVDT, LakeSolver);
	auto LakeOutflow = RegisterEquation(Model, "Lake outflow", M3PerS);
	SetSolver(Model, LakeOutflow, LakeSolver);
	auto Evaporation = RegisterEquation(Model, "Evaporation", MmPerDay);
	SetSolver(Model, Evaporation, LakeSolver);
	
	/*
        Conceptual model for water balance:
		
                              |   ^
                              |   |
                              P   E
                              |   |
                              v   | 
---Qin-->------ w ------*------------ L ------------* -- Qout -->
         \      |      /                           /  ----------
          \     |     /                           /   |
           \    |    /                           /    |
            \   h   /      V = 0.5 * w * L * h  /     h0
             \  |  /                           /      |
              \ | /                           /       |
               \|/___________________________/        |
	
		w  - lake width (m)
		h  - lake depth (m)
		h0 - depth at which outflow is 0 (m)
		L  - lake length (m)
		S  - surface area              = w * h (m^2)
		Qin - inflow (m^3/s)
		Qout - outflow (m^3/s)         = rating_curve_formula(h - h0)
		P  - precipitation (mm/day)
		E  - evaporation (mm/day)
		t  - lake slope                = 2 * h / w                    (1)
		V  - volume                    = w * L * h                    (2)
		dV/dt = (Qin - Qout)*86400 + 1e-3*(P - E)*S                   (3)
		=>
		w = 2 * h / t                                                from (1)
		V = 0.5 * L * h * (2 * h / t) = L * h^2 / t                  from (2)
		dV/dt = (L / t) d(h^2)/dt = (L / t) 2*h * dh/dt
		=>
		dh/dt = (0.5 t / (h * L)) * dV/dt  --- solve this as an ODE equation along with (3)
	*/
	
	EQUATION(Model, InitialLakeVolume,
		return 0.5 * PARAMETER(InitialWaterLevel) * PARAMETER(LakeSurfaceArea);
	)
	
	EQUATION(Model, DVDT,
		//NOTE: We don't care about ice when it comes to the water balance. We may figure out later if this matters for the computation of outflow.
		//NOTE: In the conceptualisation, the surface area is actually not constant but varies with the water level. However, that is probably not important for precip & evaporation.
		return (INPUT(LakeInflow) - RESULT(LakeOutflow)) * 86400.0 + 1e-3 * (INPUT(Precipitation) - RESULT(Evaporation)) * PARAMETER(LakeSurfaceArea);
	)
	
	EQUATION(Model, LakeVolume,
		return RESULT(DVDT);
	)
	
	EQUATION(Model, WaterLevel,
		return 0.5 * (PARAMETER(LakeShoreSlope) / (PARAMETER(LakeLength) * RESULT(WaterLevel))) * RESULT(DVDT);
	)
	
	EQUATION(Model, LakeOutflow,
		double excess = Max(0.0, RESULT(WaterLevel) - PARAMETER(WaterLevelAtWhichOutflowIsZero));
		double C3 = PARAMETER(OutflowRatingCurveShape);
		return std::pow(10.0, PARAMETER(OutflowRatingCurveMagnitude)) * (C3*excess + (1.0 - C3)*excess*excess);
	)
	
	
	//TODO: Does reference density have to be a parameter, or could it be constant?
	// Also, the units of it seem wrong??
	auto ReferenceDensity = RegisterParameterDouble(Model, PhysParams, "Reference air density", KgPerM3, 1025.0, 1000.0, 1100.0);
	auto Emissivity       = RegisterParameterDouble(Model, PhysParams, "Emissivity", Dimensionless, 0.97, 0.0, 1.0);
	auto Latitude         = RegisterParameterDouble(Model, PhysParams, "Latitude", Degrees, 60.0, -90.0, 90.0);
	
	
	
	auto SaturationSpecificHumidity           = RegisterEquation(Model, "Saturation specific humidity", KgPerKg);
	auto ActualVaporPressure                  = RegisterEquation(Model, "Actual vapor pressure", Dimensionless);   //TODO: Unit!
	auto ActualSpecificHumidity               = RegisterEquation(Model, "Actual specific humidity", KgPerKg);
	auto AirDensity                           = RegisterEquation(Model, "Air density", KgPerM3);
	auto Stability                            = RegisterEquation(Model, "Stability", Dimensionless);               //TODO: this probably has another unit
	auto TransferCoefficientForLatentHeatFlux = RegisterEquation(Model, "Transfer coefficient for latent heat flux", Dimensionless); //Correct unit?
	auto TransferCoefficientForSensibleHeatFlux = RegisterEquation(Model, "Transfer coefficient for sensible heat flux", Dimensionless); // unit?
	auto LatentHeatOfVaporization             = RegisterEquation(Model, "Latent heat of vaporization", Dimensionless); //TODO: Unit!
	auto RainfallHeatfluxCorrection           = RegisterEquation(Model, "Rainfall heat flux correction", Dimensionless); //TODO: Unit!
	auto LatentHeatFlux                       = RegisterEquation(Model, "Latent heat flux", WPerM2);
	auto SensibleHeatFlux                     = RegisterEquation(Model, "Sensible heat flux", WPerM2);
	auto LongwaveRadiation                    = RegisterEquation(Model, "Net longwave radiation", WPerM2);
	auto ShortwaveRadiation                   = RegisterEquation(Model, "Net shortwave radiation", WPerM2);
	
	auto LakeSurfaceTemperature     = RegisterEquation(Model, "Lake surface temperature", DegreesCelsius);
	//SetSolver(Model, LakeSurfaceTemperature, LakeSolver);
	//SetInitialValue;
	
	
	
	EQUATION(Model, SaturationSpecificHumidity,
		//NOTE: This assumes 'mixing ratio' ~= 'specific humidity', which is ok if vapor mass is significantly smaller than total air mass.
	
		double ratioconvertionfactor = 0.62198; //Converting molar ratio to mass ratio
		
		double svap = SaturationVaporPressure(RESULT(LakeSurfaceTemperature));
		
		return ratioconvertionfactor * svap / (INPUT(AirPressure) - 0.377 * svap); //TODO: Find out what 0.377 is for. Shouldn't that just be 1?
	)
	
	EQUATION(Model, ActualVaporPressure,
		double relhum = 0.01 * INPUT(RelativeHumidity); // percent -> fraction
		
		double svap = SaturationVaporPressure(INPUT(AirTemperature));
		return  relhum * svap;
	)
	
	EQUATION(Model, ActualSpecificHumidity,
		double ratioconvertionfactor = 0.62198;
		double actualvaporpressure = RESULT(ActualVaporPressure);
		
		return ratioconvertionfactor * actualvaporpressure / (INPUT(AirPressure) - 0.377*actualvaporpressure);
	)


	//TODO: Figure out why this eq is correct:
	EQUATION(Model, AirDensity,
		double gasconstair = 287.058;
		double ratioconvertionfactor = 0.62198;
		
		return 100.0 * INPUT(AirPressure) / (gasconstair * (INPUT(AirTemperature) + 273.15) * (1.0 + ratioconvertionfactor*RESULT(ActualSpecificHumidity)));
	)

	EQUATION(Model, Stability,
		double WW = (INPUT(WindSpeed) + 1e-10);
		double s0 = 0.25 * (RESULT(LakeSurfaceTemperature) - INPUT(AirTemperature)) / (WW * WW);
		return s0 * std::abs(s0) / (std::abs(s0) + 0.01);
	)
	
	EQUATION(Model, TransferCoefficientForLatentHeatFlux,
		double W = INPUT(WindSpeed);
		
		double ae_e; double be_e; double ce_e; double pe_e;   //NOTE: we can't use commas inside the EQUATION macro, or it screws up the comma counting of the preprocessor.
		if(W < 2.2)        { ae_e = 0.0;   be_e = 1.23;    ce_e = 0.0;     pe_e = -0.16;}
		else if (W < 5.0)  { ae_e = 0.969; be_e = 0.0521;  ce_e = 0.0;     pe_e = 1.0;  }
		else if (W < 8.0)  { ae_e = 1.18;  be_e = 0.01;    ce_e = 0.0;     pe_e = 1.0;  }
		else if (W < 25.0) { ae_e = 1.196; be_e = 0.008;   ce_e = -0.0004; pe_e = 1.0;  }
		else               { ae_e = 1.68;  be_e = -0.016;  ce_e = 0.0;     pe_e = 1.0;  }
	
		double WM8 = (W - 8.0);
		double ced = (ae_e + be_e*std::exp(pe_e * std::log(W + 1e-12)) + ce_e*WM8*WM8)*1e-3;
		
		double s = RESULT(Stability);
		if(s < 0.0)
		{
			double x;
			if(s > -3.3) 	x = 0.1 + 0.03*s + 0.9*std::exp(4.8 * s);
			else            x = 0.0;
			
			ced *= x;
		}
		else
			ced *= (1.0 + 0.63 * std::sqrt(s));
		
		return ced;
	)
	
	
	EQUATION(Model, TransferCoefficientForSensibleHeatFlux,
		double W = INPUT(WindSpeed);
		
		double ae_h; double be_h; double ce_h; double pe_h;
		if(W < 2.2)        { ae_h = 0.0;   be_h = 1.185;   ce_h = 0.0;      pe_h = -0.157;}
		else if (W < 5.0)  { ae_h = 0.927; be_h = 0.0546;  ce_h = 0.0;      pe_h = 1.0;  }
		else if (W < 8.0)  { ae_h = 1.15;  be_h = 0.01;    ce_h = 0.0;      pe_h = 1.0;  }
		else if (W < 25.0) { ae_h = 1.17;  be_h = 0.0075;  ce_h = -0.00045; pe_h = 1.0;  }
		else               { ae_h = 1.652; be_h = -0.017;  ce_h = 0.0;      pe_h = 1.0;  }
	
		double WM8 = (W - 8.0);
		double ced = (ae_h + be_h*std::exp(pe_h * std::log(W + 1e-12)) + ce_h*WM8*WM8)*1e-3;
		
		double s = RESULT(Stability);
		if(s < 0.0)
		{
			double x;
			if(s > -3.3) 	x = 0.1 + 0.03*s + 0.9*std::exp(4.8 * s);
			else            x = 0.0;
			
			ced *= x;
		}
		else
			ced *= (1.0 + 0.63 * std::sqrt(s));
		
		return ced;
	)
	
	EQUATION(Model, LatentHeatOfVaporization,
		return (2.5 - 0.00234*RESULT(LakeSurfaceTemperature))*1e6;  //TODO: Figure out unit of this!
	)
	
	EQUATION(Model, RainfallHeatfluxCorrection,
		double airt       = INPUT(AirTemperature);
		double airtkelvin = airt + 273.15;
		double cpa        = 1008.0;        //Specific heat capacity of air at constant pressure. Parameter??
		double cpw        = 3985.0;        //Specific heat capacity of water. But is that the correct number though?? Shouldn't it be 4186.0?
		double gasconstair = 287.058;
		double ratioconvertionfactor = 0.62198;
		
		double lheat = RESULT(LatentHeatOfVaporization);
		
		double x1 = 2.11e-5 * std::pow(airtkelvin/273.15, 1.94);
		double x2 = 0.02411 * (1.0 + airt*(3.309e-3 - 1.44e-6*airt)) / (RESULT(AirDensity) * cpa);
		double x3 = RESULT(ActualSpecificHumidity) * lheat / (gasconstair * airtkelvin * airtkelvin);
		
		double cd_rain = 1.0 / (1.0 + ratioconvertionfactor*x3*lheat*x1 / (cpa*x2));
		cd_rain *= cpw * ((RESULT(LakeSurfaceTemperature) - airt) + (RESULT(SaturationSpecificHumidity) - RESULT(ActualSpecificHumidity))*lheat/cpa); 
		return cd_rain / 86400.0; // 1/(kg/m2/s) -> 1/(mm/day)
	)
	
	EQUATION(Model, LatentHeatFlux,
		double latentheat = -RESULT(TransferCoefficientForLatentHeatFlux) * RESULT(LatentHeatOfVaporization) * RESULT(AirDensity) * INPUT(WindSpeed) * (RESULT(ActualSpecificHumidity) - RESULT(SaturationSpecificHumidity));
		
		//NOTE: In GOTM code they say the correct the sensible heat flux for rainfall, but actually only correct the latent heat flux! (Is this important?)
	
		//NOTE: Again, this is probably not correct for snow and ice.
		return latentheat - INPUT(Precipitation) * RESULT(RainfallHeatfluxCorrection);
	)
	
	EQUATION(Model, SensibleHeatFlux,
		double cpa        = 1008.0;
	
		return - RESULT(TransferCoefficientForSensibleHeatFlux) * cpa * RESULT(AirDensity) * INPUT(WindSpeed) * (RESULT(LakeSurfaceTemperature) - INPUT(AirTemperature));
	)
	
	EQUATION(Model, Evaporation,
		//TODO: Should we have a correction that allows for evaporation when wind=0?
		double evap = (RESULT(AirDensity) / PARAMETER(ReferenceDensity)) * RESULT(TransferCoefficientForLatentHeatFlux) * INPUT(WindSpeed) * (RESULT(ActualSpecificHumidity) - RESULT(SaturationSpecificHumidity));
		
		return -86400000.0*evap; //NOTE: Convert m/s to mm/day. Also, we want positive sign for value.
	)
	
	
	
	
	EQUATION(Model, LongwaveRadiation,
		//Net long-wave radiation. Berliand & Berliand (1952), as implemented by GOTM
		//TODO: Should maybe use different method since the cloud correction factor in this is not sophisticated.
		
		auto airtkelv = INPUT(AirTemperature) + 273.15;
		auto watertkelv = RESULT(LakeSurfaceTemperature) + 273.15;
		double boltzmannConst = 4.903e-9;
		
		double cloud = INPUT(CloudCover);
		double x1 = (1.0 - 0.6823*cloud*cloud)*airtkelv*airtkelv*airtkelv*airtkelv;
		double x2 = (0.39 - 0.05*std::sqrt(0.01*RESULT(ActualVaporPressure)));
		double x3 = 4.0 * airtkelv * airtkelv * airtkelv * (watertkelv - airtkelv);
		return - PARAMETER(Emissivity) * boltzmannConst * (x1*x2 + x3);
	)
	

	EQUATION(Model, ShortwaveRadiation,
		//Net shortwave radiation (not albedo-corrected), partially based on Rosati & Miyaconda (1988), as implemented by GOTM
		
		//TODO: This is bugged, it returns way too low numbers.
		
		double SolarConstant = 1350.0;
		double tau           = 0.7;
		double eclipse       = 23.434 * Pi / 180.0;
		double ozoneppm      = 0.09;
		
		double doy = (double)CURRENT_DAY_OF_YEAR();
		double days = (double)DAYS_THIS_YEAR();
		
		double LatitudeRad = PARAMETER(Latitude) * Pi / 180.0;
		
		
		double SolarDeclination = 0.409*sin(2.0*Pi*doy/days - 1.39);
	
		double SunsetHourAngle = acos(-tan(LatitudeRad)*tan(SolarDeclination));
		
		double cosZenitAngle = sin(LatitudeRad) * sin(SolarDeclination) + cos(LatitudeRad) * cos(SolarDeclination) * cos(SunsetHourAngle);
		
		double qatten = 0.0;
		
		if(cosZenitAngle <= 0.0)
			cosZenitAngle = 0.0;
		else
			qatten = pow(tau, 1.0/cosZenitAngle);
		
		double qzer = cosZenitAngle * SolarConstant;
		double qdir = qzer * qatten;
		double qdiff = ((1.0 - ozoneppm)*qzer - qdir) * 0.5;
		double qtot = qdir + qdiff;
		
		
		double equinox = 2.0 * Pi * (doy - 81.0) / days;
		double SinNoonAngle = sin(LatitudeRad)*sin(eclipse*sin(equinox)) + cos(LatitudeRad)*cos(eclipse*sin(equinox));
		double NoonAngleDeg = asin(SinNoonAngle) * 180.0 / Pi;
		
		double qshort = qtot * (1.0 - 0.62*INPUT(CloudCover) + 0.0019*NoonAngleDeg);
		if(qshort > qtot) qshort = qtot;
		return qshort;
	)

	
	/*
	
	
	auto IceFormationTemperature = RegisterParameterDouble(Model, PhysParams, "Ice formation temperature", DegreesCelsius, 0.0, -2.0, 2.0);
	auto IceThicknessThreshold   = RegisterParameterDouble(Model, PhysParams, "Ice thickness threshold", M, 0.1, 0.0, 1.0, "Thickness at which frazil ice solidifies");
	
	
	
	// ice energy = (PARAMETER(IceFormationTemperature) - RESULT(LakeSurfaceTemperature)) * cw * surfaceLayerThickness; // = energy available for ice formation or melting (depending on sign)
	
	// melt energy -> 0 if surfacetemp <= formationtemp or Hice = 0, otherwise -ice energy
	
	// excess melt energy : if melt energy is more than needed to melt ice.
	
	// ice thickness, frazil ice thickness, and temperature of ice (difficult to find good solution order)
	
	// -> inputs to d(lake surface temperature)/dt
	
	
	EQUATION(Model, FrazilIceThickness,
		if(LAST_RESULT(IceThickness) > 0.0)
		{
			
		}
	)
	
	EQUATION(Model, IceThickness,
		double cw = 4.18e+6;   //volumetric heat capacity of water (J K-1 m-3)
		double iceDensity = 910;  // kg m-3
		double latentHeatOfFreezing = 333500; //(J kg-1)
	
		double frazil = LAST_RESULT(FrazilIceThickness);
		double Hice = LAST_RESULT(IceThickness);
		
		double precip = INPUT(Precipitation);
	
		if(Hice > 0.0)
		{
			if(RESULT(LakeSurfaceTemperature) <= PARAMETER(IceFormationTemperature))
			{
				double initialIceEnergy = (PARAMETER(IceFormationTemperature) - RESULT(LakeSurfaceTemperature)) * cw * surfaceLayerThickness;
				frazil += ( initialIceEnergy/ (iceDensity*latentHeatOfFreezing) );
				Hice += frazil + precip*0.001;
			}
			else
			{
				//TODO
			}
		}
		else
		{
			//TODO
		}
		
		return Hice;
	)
	
	
	
	
	
	*/
	
	
	
	
	EQUATION(Model, LakeSurfaceTemperature,
		return INPUT(AirTemperature);       //TODO!!!!
	)
	
	
	
	EndModule(Model);
}