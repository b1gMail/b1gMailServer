using System;
using System.Collections.Generic;
using System.Text;
using System.ServiceProcess;
using System.Management;

namespace BMSManager
{
    public class BMSServiceController : ServiceController
    {
        public BMSServiceController() : base()
        {
        }

        public BMSServiceController(string name) : base(name)
        {
        }

        public BMSServiceController(string name, string machineName) : base(name, machineName)
        {
        }

        public ServiceStartMode StartMode
        {
            get
            {
                if (this.ServiceName != null)
                {
                    string path = "Win32_Service.Name='" + this.ServiceName + "'";
                    ManagementPath p = new ManagementPath(path);
                    ManagementObject o = new ManagementObject(p);

                    switch(o["StartMode"].ToString())
                    {
                        case "Auto":
                            return(ServiceStartMode.Automatic);
                        case "Manual":
                            return(ServiceStartMode.Manual);
                        case "Disabled":
                            return(ServiceStartMode.Disabled);
                    }
                }
                
                return (ServiceStartMode.Disabled);
            }

            set
            {
                if (this.ServiceName != null)
                {
                    string path = "Win32_Service.Name='" + this.ServiceName + "'";
                    ManagementPath p = new ManagementPath(path);
                    ManagementObject o = new ManagementObject(p);

                    object[] parameters = new object[1];

                    if (value == ServiceStartMode.Automatic)
                        parameters[0] = "Automatic";
                    else if (value == ServiceStartMode.Disabled)
                        parameters[0] = "Disabled";
                    else if (value == ServiceStartMode.Manual)
                        parameters[0] = "Manual";

                    o.InvokeMethod("ChangeStartMode", parameters);
                }
            }
        }
    }
}
