/*arrayPerformanceMain.cpp */
/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * EPICS pvData is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */
/**
 * @author mrk
 * @date 2013.08.08
 */

/* Author: Marty Kraimer */

#include <cstddef>
#include <cstdlib>
#include <cstddef>
#include <string>
#include <cstdio>
#include <memory>
#include <iostream>
#include <vector>

#include <pv/standardField.h>
#include <pv/standardPVField.h>
#include <pv/arrayPerformance.h>
#include <pv/longArrayMonitor.h>
#include <pv/traceRecord.h>
#include <pv/channelProviderLocal.h>
#include <pv/serverContext.h>
#include <pv/clientFactory.h>

using namespace std;
using std::tr1::static_pointer_cast;
using namespace epics::pvData;
using namespace epics::pvAccess;
using namespace epics::pvDatabase;

int main(int argc,char *argv[])
{
    bool result(false);
    String recordName;
    recordName = "arrayPerformance";
    size_t size = 50000000;
    double delay = .01;
    String providerName("local");
    size_t nMonitor = 1;
    bool useQueue = false;
    if(argc==2 && String(argv[1])==String("-help")) {
        cout << "arrayPerformanceMain recordName size";
        cout << " delay providerName nMonitor useQueue" << endl;
        cout << "default" << endl;
        cout << "arrayPerformance ";
        cout << recordName << " ";
        cout << size << " ";
        cout << delay << " ";
        cout << providerName << " ";
        cout << nMonitor << " ";
        cout << (useQueue ? "true" : "false") << endl;
        return 0;
    }
    if(argc>1) recordName = argv[1];
    if(argc>2) size = strtoul(argv[2],0,0);
    if(argc>3) delay = atof(argv[3]);
    if(argc>4) providerName = argv[4];
    if(argc>5) nMonitor = strtoul(argv[5],0,0);
    if(argc>6) useQueue = (argv[6]==String("true") ? true : false);
    cout << "arrayPerformance ";
    cout << recordName << " ";
    cout << size << " ";
    cout << delay << " ";
    cout << providerName << " ";
    cout << nMonitor << " ";
    cout << (useQueue ? "true" : "false") << endl;
    ClientFactory::start();
    PVDatabasePtr master = PVDatabase::getMaster();
    ChannelProviderLocalPtr channelProvider = getChannelProviderLocal();
    PVRecordPtr pvRecord;
    pvRecord = ArrayPerformance::create(recordName,size,delay);
    result = master->addRecord(pvRecord);
    pvRecord = TraceRecord::create("traceRecordPGRPC");
    result = master->addRecord(pvRecord);
    if(!result) cout<< "record " << recordName << " not added" << endl;
    pvRecord.reset();
    ServerContext::shared_pointer pvaServer = 
        startPVAServer(PVACCESS_ALL_PROVIDERS,0,true,true);
    std::vector<LongArrayMonitorPtr> longArrayMonitor(nMonitor);
    for(size_t i=0; i<nMonitor; ++i) {
       longArrayMonitor[i]
         = LongArrayMonitor::create(providerName,recordName,useQueue);
    }
    for(size_t i=0; i<nMonitor; ++i) longArrayMonitor[i]->start();
    cout << "arrayPerformance\n";
    string str;
    while(true) {
        cout << "Type exit to stop: \n";
        getline(cin,str);
        if(str.compare("exit")==0) break;

    }
    for(size_t i=0; i<nMonitor; ++i) longArrayMonitor[i]->stop();
    pvaServer->shutdown();
    epicsThreadSleep(1.0);
    pvaServer->destroy();
    ClientFactory::stop();
    channelProvider->destroy();
    return 0;
}

