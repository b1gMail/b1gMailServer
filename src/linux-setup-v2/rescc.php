<?php
$outFileName = $_SERVER['argv'][1];
if(!$outFileName)
{
	die('Not output file name given!');
}

$fileNames = array();
$lengths = array();
$outFP = fopen($outFileName, 'wb');

fwrite($outFP, "#include \"resdata.h\"\n\n");

fwrite($outFP, "const char *_resData[] =\n{\n");

$d = dir('./res/');
while($entry = $d->read())
{
	if(substr($entry, 0, 1) == '.') continue;
	if(is_dir('./res/' . $entry)) continue;

	$fileNames[] = strtolower($entry);
	$lengths[] = filesize('./res/' . $entry);

	fwrite($outFP, "//\n// $entry\n//\n");
	fwrite($outFP, "\t\"");

	$i = 0;
	$inFP = fopen('./res/' . $entry, 'rb');
	while(!feof($inFP))
	{
		$i++;
		$c = fgetc($inFP);

		if(feof($inFP))
			break;

		fprintf($outFP, "\\x%02x", ord($c));

		if($i == 32)
		{
			fwrite($outFP, "\"\n\t\"");
			$i = 0;
		}
	}
	fclose($inFP);

	fwrite($outFP, "\",\n\n");
}
$d->close();

fwrite($outFP, "\t0\n};\n\n");
fwrite($outFP, "const char *_resNames[] = {\n");
foreach($fileNames as $resName)
{
	fprintf($outFP, "\t\"%s\",\n", $resName);
}
fwrite($outFP, "\t0\n};\n\n");

fwrite($outFP, "const int _resLengths[] = {\n\t");

fwrite($outFP, implode(",\n\t", $lengths));
fwrite($outFP, ",\n\t0\n};\n");

fclose($outFP);
