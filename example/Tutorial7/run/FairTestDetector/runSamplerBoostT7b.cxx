/********************************************************************************
 *    Copyright (C) 2014 GSI Helmholtzzentrum fuer Schwerionenforschung GmbH    *
 *                                                                              *
 *              This software is distributed under the terms of the             *
 *         GNU Lesser General Public Licence version 3 (LGPL) version 3,        *
 *                  copied verbatim in the file "LICENSE"                       *
 ********************************************************************************/
/*
 * File:   runSamplerBoost.cxx
 * Author: winckler
 *
 * Created on December 2, 2014, 10:45 PM
 */

// std
#include <iostream>
#include <csignal>

// boost
#include "boost/program_options.hpp"
#include <boost/archive/binary_oarchive.hpp>

// FairRoot
#include "FairMQLogger.h"
#ifdef NANOMSG
#include "nanomsg/FairMQTransportFactoryNN.h"
#else
#include "zeromq/FairMQTransportFactoryZMQ.h"
#endif

// FairRoot - Tutorial 7
// Sampler device
#include "GenericSampler.h"

// sampler policy
#include "SimpleTreeReader.h"

// serialization policy
#include "BoostSerializer.h"

// payload/data class
#include "FairTestDetectorDigi.h"

/// ROOT
#include "TSystem.h"

using namespace std;
/// ////////////////////////////////////////////////////////////////////////
// payload definition
typedef FairTestDetectorDigi                            TDigi;
// sampler and serialization policy
typedef SimpleTreeReader<TClonesArray>                 TSamplerPolicy;
typedef BoostSerializer<TDigi>                          TSerializePolicy;
// build sampler type
typedef GenericSampler<TSamplerPolicy,TSerializePolicy> TSampler;

TSampler sampler;

static void s_signal_handler(int signal)
{
    LOG(INFO) << "Caught signal " << signal;

    sampler.ChangeState(TSampler::END);

    LOG(INFO) << "Shutdown complete.";
    exit(1);
}

static void s_catch_signals(void)
{
    struct sigaction action;
    action.sa_handler = s_signal_handler;
    action.sa_flags = 0;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
}

typedef struct DeviceOptions
{
    string id;
    string filename;
    string parameterFile;
    string treename;
    string branchname;
    int eventRate;
    int ioThreads;
    string outputSocketType;
    int outputBufSize;
    string outputMethod;
    string outputAddress;
} DeviceOptions_t;

inline bool parse_cmd_line(int _argc, char* _argv[], DeviceOptions* _options)
{
    if (_options == NULL)
        throw std::runtime_error("Internal error: options' container is empty.");

    namespace bpo = boost::program_options;
    bpo::options_description desc("Options");
    desc.add_options()
        ("id", bpo::value<string>()->required(), "Device ID")
        ("input-file", bpo::value<string>()->required(), "Path to the input file")
        ("tree", bpo::value<string>()->default_value("T7DataTree"), "Name of the tree")
        ("branch", bpo::value<string>()->default_value("T7digidata"), "Name of the Branch")
        ("event-rate", bpo::value<int>()->default_value(0), "Event rate limit in maximum number of events per second")
        ("io-threads", bpo::value<int>()->default_value(1), "Number of I/O threads")
        ("output-socket-type", bpo::value<string>()->required(), "Output socket type: pub/push")
        ("output-buff-size", bpo::value<int>()->required(), "Output buffer size in number of messages (ZeroMQ)/bytes(nanomsg)")
        ("output-method", bpo::value<string>()->required(), "Output method: bind/connect")
        ("output-address", bpo::value<string>()->required(), "Output address, e.g.: \"tcp://*:5555\"")
        ("help", "Print help messages");

    bpo::variables_map vm;
    bpo::store(bpo::parse_command_line(_argc, _argv, desc), vm);

    if ( vm.count("help") )
    {
        LOG(INFO) << "Tutorial 7 - Sampler " << endl << desc;
        return false;
    }

    bpo::notify(vm);

    if ( vm.count("id") )
        _options->id = vm["id"].as<string>();

    if ( vm.count("input-file") )
        _options->filename = vm["input-file"].as<string>();

    if ( vm.count("branch") )
        _options->branchname = vm["branch"].as<string>();

    if ( vm.count("tree") )
        _options->treename = vm["tree"].as<string>();
    
    if ( vm.count("event-rate") )
        _options->eventRate = vm["event-rate"].as<int>();

    if ( vm.count("io-threads") )
        _options->ioThreads = vm["io-threads"].as<int>();

    if ( vm.count("output-socket-type") )
        _options->outputSocketType = vm["output-socket-type"].as<string>();

    if ( vm.count("output-buff-size") )
        _options->outputBufSize = vm["output-buff-size"].as<int>();

    if ( vm.count("output-method") )
        _options->outputMethod = vm["output-method"].as<string>();

    if ( vm.count("output-address") )
        _options->outputAddress = vm["output-address"].as<string>();

    return true;
}

int main(int argc, char** argv)
{
    try
    {
        s_catch_signals();
        gSystem->Load("libTree.so");
        DeviceOptions_t options;
        try
        {
            if (!parse_cmd_line(argc, argv, &options))
                return 0;
        }
        catch (std::exception& err)
        {
            LOG(ERROR) << err.what();
            return 1;
        }

        LOG(INFO) << "PID: " << getpid();
        LOG(INFO) << "CONFIG: " << "id: " << options.id << ", event rate: " << options.eventRate << ", I/O threads: " << options.ioThreads;
        LOG(INFO) << "FILES: " << "input file: " << options.filename << ", parameter file: " << options.parameterFile << ", branch: " << options.branchname;
        LOG(INFO) << "OUTPUT: " << options.outputSocketType << " " << options.outputBufSize << " " << options.outputMethod << " " << options.outputAddress;

    #ifdef NANOMSG
        FairMQTransportFactory* transportFactory = new FairMQTransportFactoryNN();
    #else
        FairMQTransportFactory* transportFactory = new FairMQTransportFactoryZMQ();
    #endif

        sampler.SetTransport(transportFactory);

        FairMQChannel channel(options.outputSocketType, options.outputMethod, options.outputAddress);
        channel.UpdateSndBufSize(options.outputBufSize);
        channel.UpdateRcvBufSize(options.outputBufSize);
        channel.UpdateRateLogging(1);

        sampler.fChannels["data-out"].push_back(channel);

        sampler.SetProperty(TSampler::Id, options.id);
        sampler.SetProperty(TSampler::InputFile, options.filename);
        sampler.SetProperty(TSampler::ParFile, options.parameterFile);
        sampler.SetProperty(TSampler::Branch, options.branchname);
        sampler.SetProperty(TSampler::EventRate, options.eventRate);
        sampler.SetProperty(TSampler::NumIoThreads, options.ioThreads);
        sampler.SetFileProperties(options.filename, options.treename, options.branchname);

        sampler.ChangeState(TSampler::INIT_DEVICE);
        sampler.WaitForEndOfState(TSampler::INIT_DEVICE);

        sampler.ChangeState(TSampler::INIT_TASK);
        sampler.WaitForEndOfState(TSampler::INIT_TASK);

        sampler.ChangeState(TSampler::RUN);
        sampler.WaitForEndOfState(TSampler::RUN);

        sampler.ChangeState(TSampler::STOP);

        sampler.ChangeState(TSampler::RESET_TASK);
        sampler.WaitForEndOfState(TSampler::RESET_TASK);

        sampler.ChangeState(TSampler::RESET_DEVICE);
        sampler.WaitForEndOfState(TSampler::RESET_DEVICE);

        sampler.ChangeState(TSampler::END);
    }
    catch (std::exception& e)
    {
        LOG(ERROR)  << "Unhandled Exception reached the top of main: " 
                    << e.what() << ", application will now exit";
        return 1;
    }
    
    return 0;
}


