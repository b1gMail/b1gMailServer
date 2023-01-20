#include <stdio.h>
#include <time.h>

int main(int argc, char *argv[])
{
	time_t timeT = time(NULL);
	FILE *fp1, *fp2;
	int build = 0;
	struct tm *tt = localtime(&timeT);
	char *szDate = asctime(tt);

	szDate[24] = '\0';

	fp2 = fopen("buildno", "r");
	fscanf(fp2, "%d", &build);
	build++;
	fclose(fp2);

	fp2 = fopen("buildno", "w");
	fprintf(fp2, "%d", build);
	fclose(fp2);

	fp1 = fopen("build.h", "w+");
	fprintf(fp1, "#define BMS_BUILD \"%d\"\n", build);
	fprintf(fp1, "#define BMS_BUILD_NUM %d\n", build);
	fprintf(fp1, "#define BMS_BUILD_DATE \"%s\"\n", szDate);
	fprintf(fp1, "#define BMS_BUILD_ARCH \"" ARCH "\"\n");
	fclose(fp1);

	fp1 = fopen("BMSManager/BMSManager/Properties/AssemblyInfo.cs", "w+");
	fprintf(fp1, "using System.Reflection;\n"
		"using System.Runtime.CompilerServices;\n"
		"using System.Runtime.InteropServices;\n"
		"[assembly: AssemblyTitle(\"b1gMailServer Manager\")]\n"
		"[assembly: AssemblyDescription(\"b1gMailServer Manager\")]\n"
		"[assembly: AssemblyConfiguration(\"\")]\n"
		"[assembly: AssemblyCompany(\"B1G Software\")]\n"
		"[assembly: AssemblyProduct(\"b1gMailServer\")]\n"
		"[assembly: AssemblyCopyright(\"Copyright (C) 2002-2018 B1G Software\")]\n"
		"[assembly: AssemblyTrademark(\"\")]\n"
		"[assembly: AssemblyCulture(\"\")]\n"
		"[assembly: ComVisible(false)]\n"
		"[assembly: Guid(\"55c6db62-655f-479f-9fe1-4b19f7c73760\")]\n"
		"[assembly: AssemblyVersion(\"2.8.%d.0\")]\n"
		"[assembly: AssemblyFileVersion(\"2.8.%d.0\")]\n",
		build, build);
	fclose(fp1);
}
