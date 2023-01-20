<!--

function EBID(f)
{
	return(document.getElementById(f));
}

function MakeXMLRequest(url, data, callback)
{
	var xmlHTTP = false;

	if(typeof(XMLHttpRequest) != "undefined")
	{
		xmlHTTP = new XMLHttpRequest();
	}
	else
	{
		try
		{
			xmlHTTP = new ActiveXObject("Msxml2.XMLHTTP");
		}
		catch(e)
		{
			try
			{
				xmlHTTP = new ActiveXObject("Microsoft.XMLHTTP");
			}
			catch(e)
			{
			}
		}
	}

	if(!xmlHTTP)
	{
		return(false);
	}
	else
	{
		xmlHTTP.open("POST", url, true);
		xmlHTTP.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
		xmlHTTP.setRequestHeader("Content-Length", data.length);

		if(typeof(callback) == "string")
		{
			xmlHTTP.onreadystatechange = function xh_readyChange()
				{
					eval(callback + "(xmlHTTP)");
				}
		}
		else if(callback != null)
		{
			xmlHTTP.onreadystatechange = function xh_readyChangeCallback()
				{
					callback(xmlHTTP);
				}
		}

		xmlHTTP.send(data);

		return(true);
	}
}

function _checkPortStatus(e)
{
	if(e.readyState == 4)
	{
		var response = e.responseText,
			parts = response.split(':');
		
		if(parts.length == 2)
		{
			var svc = parts[0], status = parts[1];

			if(EBID(svc + 'PortStatus'))
				EBID(svc + 'PortStatus').src = (status == '1' ? '/res/ok.png' : '/res/error.png');
		}
	}
}

function checkPortStatus(svc)
{
	if(EBID(svc) && EBID(svc).checked)
	{
		if(EBID(svc + 'PortStatus'))
		{
			EBID(svc + 'PortStatus').style.display = '';
			EBID(svc + 'PortStatus').src = '/res/load_16.gif';
		}

		var iface = EBID(svc + 'Address').value, port = EBID(svc + 'Port').value;

		MakeXMLRequest('/rpc',
						'action=checkPortStatus&interface=' + escape(iface) + '&port=' + escape(port) + '&svc=' + escape(svc),
						_checkPortStatus);
	}
	else
	{
		if(EBID(svc + 'PortStatus'))
		{
			EBID(svc + 'PortStatus').style.display = 'none';
		}
	}
}

//-->
