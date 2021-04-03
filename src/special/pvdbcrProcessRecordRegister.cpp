/*
 * Copyright information and license terms for this software can be
 * found in the file LICENSE that is included with the distribution
 */

/**
 * @author mrk
 * @date 2021.03.12
 */
#include <epicsThread.h>
#include <epicsGuard.h>
#include <pv/event.h>
#include <pv/lock.h>
#include <iocsh.h>
#include <pv/standardField.h>
#include <pv/standardPVField.h>
#include <pv/timeStamp.h>
#include <pv/pvTimeStamp.h>
#include <pv/alarm.h>
#include <pv/pvAlarm.h>
#include <pv/pvAccess.h>
#include <pv/serverContext.h>

#include <epicsExport.h>
#define epicsExportSharedSymbols
#include "pv/pvStructureCopy.h"
#include "pv/channelProviderLocal.h"
#include "pv/pvDatabase.h"

using namespace epics::pvData;
using namespace epics::pvDatabase;
using namespace std;

typedef std::tr1::shared_ptr<epicsThread> EpicsThreadPtr;
class PvdbcrProcessRecord;
typedef std::tr1::shared_ptr<PvdbcrProcessRecord> PvdbcrProcessRecordPtr;

class epicsShareClass PvdbcrProcessRecord :
    public PVRecord,
    public epicsThreadRunable
{
public:
    POINTER_DEFINITIONS(PvdbcrProcessRecord);
    static PvdbcrProcessRecordPtr create(
        std::string const & recordName,double delay);
    virtual bool init();
    virtual void process();
    virtual void run();
    void startThread();
    void stop();
private:
    PvdbcrProcessRecord(
        std::string const & recordName,
        epics::pvData::PVStructurePtr const & pvStructure,double delay);
    double delay;
    EpicsThreadPtr thread;
    epics::pvData::Event runStop;
    epics::pvData::Event runReturn;
    PVDatabasePtr pvDatabase;
    PVRecordMap pvRecordMap;
    epics::pvData::PVStringPtr pvCommand;
    epics::pvData::PVStringPtr pvRecordName;
    epics::pvData::PVStringPtr pvResult;
    epics::pvData::Mutex mutex;
};

PvdbcrProcessRecordPtr PvdbcrProcessRecord::create(
    std::string const & recordName,double delay)
{
    FieldCreatePtr fieldCreate = getFieldCreate();
    PVDataCreatePtr pvDataCreate = getPVDataCreate();
    StructureConstPtr  topStructure = fieldCreate->createFieldBuilder()->
        addNestedStructure("argument")->
            add("command",pvString)->
            add("recordName",pvString)->
            endNested()->
        addNestedStructure("result") ->
            add("status",pvString) ->
            endNested()->
        createStructure();
    PVStructurePtr pvStructure = pvDataCreate->createPVStructure(topStructure);
    PvdbcrProcessRecordPtr pvRecord(
        new PvdbcrProcessRecord(recordName,pvStructure,delay));
    if(!pvRecord->init()) pvRecord.reset();
    return pvRecord;
}

void PvdbcrProcessRecord::startThread()
{
    thread = EpicsThreadPtr(new epicsThread(
        *this,
        "processRecord",
        epicsThreadGetStackSize(epicsThreadStackSmall),
        epicsThreadPriorityLow));
    thread->start();
}

void PvdbcrProcessRecord::stop()
{
    runStop.signal();
    runReturn.wait();
}


PvdbcrProcessRecord::PvdbcrProcessRecord(
    std::string const & recordName,
    epics::pvData::PVStructurePtr const & pvStructure,double delay)
: PVRecord(recordName,pvStructure),
  delay(delay),
  pvDatabase(PVDatabase::getMaster())
{
}

bool PvdbcrProcessRecord::init()
{
    initPVRecord();
    PVStructurePtr pvStructure = getPVStructure();
    pvCommand = pvStructure->getSubField<PVString>("argument.command");
    pvRecordName = pvStructure->getSubField<PVString>("argument.recordName");
    if(!pvRecordName) return false;
    pvResult = pvStructure->getSubField<PVString>("result.status");
    if(!pvResult) return false;
    startThread();
    return true;
}

void PvdbcrProcessRecord::process()
{
    string recordName = pvRecordName->get();
    string command = pvCommand->get();
    if(command.compare("add")==0) {
        epicsGuard<epics::pvData::Mutex> guard(mutex);
        std::map<std::string,PVRecordPtr>::iterator iter = pvRecordMap.find(recordName);
        if(iter!=pvRecordMap.end()) {
             pvResult->put(recordName + " already present");
             return;
        }
        PVRecordPtr pvRecord = pvDatabase->findRecord(recordName);
        if(!pvRecord) {
             pvResult->put(recordName + " not in pvDatabase");
             return;
        }
        pvRecordMap.insert(PVRecordMap::value_type(recordName,pvRecord));
        pvResult->put("success");
        return;
    } else if(command.compare("remove")==0) {
        epicsGuard<epics::pvData::Mutex> guard(mutex);
        std::map<std::string,PVRecordPtr>::iterator iter = pvRecordMap.find(recordName);
        if(iter==pvRecordMap.end()) {
             pvResult->put(recordName + " not found");
             return;
        }
        pvRecordMap.erase(iter);
        pvResult->put("success");
        return;
    } else {
        pvResult->put(command  + " not a valid command: only add and remove are valid");
        return;
    }
}

void PvdbcrProcessRecord::run()
{
    while(true) {
        if(runStop.tryWait()) {
             runReturn.signal();
             return;
        }
        if(delay>0.0) epicsThreadSleep(delay);
        epicsGuard<epics::pvData::Mutex> guard(mutex);
        PVRecordMap::iterator iter;
        for(iter = pvRecordMap.begin(); iter!=pvRecordMap.end(); ++iter) {
           PVRecordPtr pvRecord = (*iter).second;
           pvRecord->lock();
           pvRecord->beginGroupPut();
           try {
               pvRecord->process();
           } catch (std::exception& ex) {
               std::cout << "record " << pvRecord->getRecordName() << "exception " << ex.what() << "\n";
           } catch (...) {
               std::cout<< "record " << pvRecord->getRecordName() << " process exception\n";
           }
           pvRecord->endGroupPut();
           pvRecord->unlock();
        }
    }
}

static const iocshArg arg0 = { "recordName", iocshArgString };
static const iocshArg arg1 = { "delay", iocshArgDouble };
static const iocshArg arg2 = { "asLevel", iocshArgInt };
static const iocshArg arg3 = { "asGroup", iocshArgString };
static const iocshArg *args[] = {&arg0,&arg1,&arg2,&arg3};

static const iocshFuncDef pvdbcrProcessRecordFuncDef = {"pvdbcrProcessRecord", 4,args};

static void pvdbcrProcessRecordCallFunc(const iocshArgBuf *args)
{
    char *sval = args[0].sval;
    if(!sval) {
        throw std::runtime_error("pvdbcrProcessRecord recordName not specified");
    }
    string recordName = string(sval);
    double delay = args[1].dval;
    if(delay<0.0) delay = 1.0;
    int asLevel = args[2].ival;
    string asGroup("DEFAULT");
    sval = args[3].sval;
    if(sval) {
        asGroup = string(sval);
    }
    PvdbcrProcessRecordPtr record = PvdbcrProcessRecord::create(recordName,delay);
    record->setAsLevel(asLevel);
    record->setAsGroup(asGroup);
    PVDatabasePtr master = PVDatabase::getMaster();
    bool result =  master->addRecord(record);
    if(!result) cout << "recordname " << recordName << " not added" << endl;
}

static void pvdbcrProcessRecordRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&pvdbcrProcessRecordFuncDef, pvdbcrProcessRecordCallFunc);
    }
}

extern "C" {
    epicsExportRegistrar(pvdbcrProcessRecordRegister);
}
