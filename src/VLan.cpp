#include <VLan.h>
#include <Debug.h>
#include <Macros.h>
#include <ExecUtils.h>
#include <sstream>
#include <aim_types.h>

VLan::VLan() : Service("VLAN")
{
}

VLan::~VLan()
{
}

bool VLan::initialize(INIReader configuration)
{
    return true;
}

bool VLan::start()
{
    return true;
}

bool VLan::stop()
{
    return true;
}

bool VLan::cleanup()
{
    return true;
}

void VLan::throwError(const string& message)
{
    VLanException exception;
    exception.description = message;

    throw exception;
}

void VLan::createVLAN(int vlan, const string& vlanInterface, const string& bridgeInterface)
{
    boost::mutex::scoped_lock lock(create_vlan_mutex);
    
    if (!createBridgeInterface(vlanInterface, vlan, bridgeInterface))
    {
        ostringstream error;
        error << "Error creating bridge interface " << bridgeInterface;
        error.flush();

        LOG("%s", error.str().c_str());
        throwError(error.str());
    }

    if (!createVLANInterface(vlanInterface, vlan))
    {
        ostringstream error;
        error << "Error creating VLAN with tag " << vlan << " and interface " << vlanInterface;
        error.flush();

        LOG("%s", error.str().c_str());
        throwError(error.str());
    }

    LOG("VLan created, tag=%d, interface=%s, bridge=%s", vlan, vlanInterface.c_str(), bridgeInterface.c_str());
}

bool VLan::createVLANInterface(const string& vlanIf, int vlan)
{
    if (!existsVlan(vlan, vlanIf))
    {
        string filename = buildVLANFilename(vlan, vlanIf);

        if (!writeVLANConfiguration(vlanIf, vlan, NETWORK_SCRIPTS_FOLDER, filename))
        {
            return false;
        }

        if (!ifUp(filename))
        {
            LOG("Unable to tear up the VLAN interface %s", vlanIf.c_str());
            return false;
        }
    }
    else
    {
        LOG("VLAN with tag %d and interface %s already exists.", vlan, vlanIf.c_str());
    }

    return true;
}

bool VLan::createBridgeInterface(const string& vlanIf, int vlan, const string& bridgeIf)
{
    if (!existsBridge(bridgeIf))
    {
        string filename = buildBridgeFilename(bridgeIf);

        if (!writeBridgeConfiguration(vlanIf, vlan, bridgeIf, NETWORK_SCRIPTS_FOLDER, filename))
        {
            return false;
        }

        if (!ifUp(filename))
        {
            LOG("Unable to tear up the bridge %s", bridgeIf.c_str());
            return false;
        }
    }
    else
    {
        LOG("Bridge interface %s already exists.", bridgeIf.c_str());

        ostringstream oss;
        oss << ifconfig << " " << bridgeIf << " up" << " > /dev/null 2>/dev/null";
        oss.flush();
        return (executeCommand(oss.str()) == 0);
    }

    return true;
}

bool VLan::ifUp(string& filename)
{
    ostringstream command;
    command << "ifup " << filename;
    command.flush();

    return (executeCommand(command.str()) == 0);
}

bool VLan::ifDown(string& filename)
{
    ostringstream command;
    command << "ifdown " << filename;
    command.flush();

    return (executeCommand(command.str()) == 0);
}

string VLan::buildVLANFilename(int vlan, const string& vlanIf)
{
    ostringstream oss;
    oss << vlanIf << "." << vlan;
    oss.flush();

    return oss.str();
}

string VLan::buildBridgeFilename(const string& bridgeIf)
{
    ostringstream oss;
    oss << bridgeIf;
    oss.flush();

    return oss.str();
}

void VLan::deleteVLAN(int vlan, const string& vlanInterface, const string& bridgeInterface)
{
    boost::mutex::scoped_lock lock(delete_vlan_mutex);

    if(!deleteBridgeInterface(bridgeInterface))
    {
        ostringstream error;
        error << "Error deleting bridge interface " << bridgeInterface;
        error.flush();

        LOG("%s", error.str().c_str());
        throwError(error.str());
    }

    if (!deleteVLANInterface(vlan, vlanInterface))
    {   
        ostringstream error;
        error << "Error deleting VLAN interface " << vlanInterface;
        error.flush();

        LOG("%s", error.str().c_str());
        throwError(error.str());
    }

    LOG("VLan deleted, tag=%d, interface=%s, bridge=%s", vlan, vlanInterface.c_str(), bridgeInterface.c_str());
}

bool VLan::deleteBridgeInterface(const string& bridgeIf)
{
    if (existsBridge(bridgeIf))
    {
        string filename = buildBridgeFilename(bridgeIf);

        if (!ifDown(filename))
        {
            LOG("Unable to tear down the bridge %s", bridgeIf.c_str());
            return false;
        }

        if (!removeFile(NETWORK_SCRIPTS_FOLDER, filename))
        {
            // TODO try to do an up?
            ifUp(filename);
            return false;
        }

        // Ensure the bridge is correctly destroyed
        if (existsBridge(bridgeIf))
        {
            if (!removeBridge(bridgeIf)) 
            {
                LOG("The %s cannot be destroyed. This can create consistency problems", bridgeIf.c_str());
                return false;
            }
        }
    }
    else
    {
        LOG("Bridge interface %s does not exist.", bridgeIf.c_str());
    }

    return true;
}

bool VLan::deleteVLANInterface(int vlan, const string& vlanIf)
{
    if (existsVlan(vlan, vlanIf))
    {
        string filename = buildVLANFilename(vlan, vlanIf);

        if (!ifDown(filename))
        {
            LOG("Unable to tear down the VLAN interface %s", vlanIf.c_str());
            return false;
        }

        if (!removeFile(NETWORK_SCRIPTS_FOLDER, filename))
        {
            // TODO try to do an up?
            ifUp(filename);

            return false;
        }
    }
    else
    {
        LOG("VLAN with tag %d and interface %s does not exist.", vlan, vlanIf.c_str());
    }

    return true;
}

void VLan::checkVlanRange(const int vlan)
{
    if (vlan < 1 || vlan > 4094)
    {
        ostringstream oss;

        oss << "VLAN tag out of range (" << vlan << ").";
        oss.flush();

        LOG("%s", oss.str().c_str());
        throwError(oss.str());
    }
}

bool VLan::existsVlan(const int vlan, const string& vlanInterface)
{
    checkVlanRange(vlan);

    ostringstream oss;

    oss << vlanInterface << "." << vlan;
    oss.flush();

    return existsInterface(oss.str());
}

bool VLan::existsInterface(const string& interface)
{
    ostringstream oss;

    oss << ifconfig << " -a " << interface << " > /dev/null 2>/dev/null";
    oss.flush();

    return (executeCommand(oss.str()) == 0);
}

bool VLan::existsBridge(const string& interface)
{
    return existsInterface(interface);
}

void VLan::checkVLANConfiguration()
{
    string error = "Failed to check the command/s: ";
    bool ok = true;

    if (executeCommand(ifconfig) == 127)
    {
        ok = false;
        error.append(ifconfig).append(" ");
    }

    if (executeCommand(brctl) == 127)
    {
        ok = false;
        error.append(brctl).append(" ");
    }   

    if (!ok)
    {
        LOG("%s", error.c_str());
        throwError(error);
    }
}

bool VLan::writeVLANConfiguration(const string& device, int vlan, const string& folder, const string& filename)
{
    if (isAccessible(folder))
    {
        // Compose filename
        ostringstream filepath; 
        filepath << folder << "/" << filename;

        // Write config file
        ofstream config;

        config.open(filepath.str().c_str(), ios_base::trunc);
        config << "# Autogenerated by Abiquo AIM" << endl << endl;
        config << "auto " << device << "." << vlan << endl;
        config << "iface " << device << "." << vlan << " inet manual" << endl;
        config.close();

        LOG("The VLAN configuration has been written in '%s'", filepath.str().c_str());
        return true;
    }

    LOG("Unable to write the VLAN configuration '%s' is not accessible", folder.c_str());
    return false;
}

bool VLan::writeBridgeConfiguration(const string& vlanIf, int vlan, const string& bridgeName, const string& folder, const string& filename)
{
    if (isAccessible(folder))
    {
        // Compose filename
        ostringstream filepath;
        filepath << folder << "/" << filename;

        // Write config file
        ofstream config;

        config.open(filepath.str().c_str(), ios_base::trunc);
        config << "# Autogenerated by Abiquo AIM" << endl << endl;
        config << "auto " << bridgeName << endl;
        config << "iface " << bridgeName << " inet manual" << endl;
        config << "    bridge_ports " << vlanIf << "." << vlan << endl;
        config << "    bridge_stp on" << endl;
        config.close();

        LOG("The bridge configuration configuration has been written in '%s'", filepath.str().c_str());
        return true;
    }

    LOG("Unable to write the bridge configuration '%s' is not accessible", folder.c_str());
    return false;
}

bool VLan::isAccessible(const string& path)
{
    if (access(path.c_str(), F_OK | R_OK | W_OK) == -1)
    {
        return false;
    }

    return true;
}

bool VLan::removeFile(const string& folder, const string& filename)
{
    ostringstream oss;
    oss << folder << "/" << filename;

    string filepath = oss.str();

    if (!isAccessible(folder))
    {
        LOG("Unable to remove %s, folder %s is no accessible", filepath.c_str(), folder.c_str());
        return false;
    }

    if (unlink(filepath.c_str()) != 0)
    {
        LOG("Unable to remove %s", filepath.c_str());
        return false;
    }

    return true;
}

bool VLan::removeBridge(const string& bridgeIf)
{
    ostringstream command;
    command << "/sbin/brctl delbr " << bridgeIf;
    command.flush();

    return (executeCommand(command.str()) == 0);
}

