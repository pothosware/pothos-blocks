// Copyright (c) 2014-2017 Josh Blum
//                    2020 Nicholas Corgan
// SPDX-License-Identifier: BSL-1.0

#include <Pothos/Testing.hpp>
#include <Pothos/Framework.hpp>
#include <Pothos/Proxy.hpp>

#include <json.hpp>

#include <Poco/Random.h>
#include <Poco/RandomStream.h>
#include <Poco/TemporaryFile.h>
#include <Poco/Thread.h>

using json = nlohmann::json;

POTHOS_TEST_BLOCK("/blocks/tests", test_binary_file_blocks)
{
    auto feeder = Pothos::BlockRegistry::make("/blocks/feeder_source", "int");
    auto collector = Pothos::BlockRegistry::make("/blocks/collector_sink", "int");

    auto tempFile = Poco::TemporaryFile();
    POTHOS_TEST_TRUE(tempFile.createFile());

    auto fileSource = Pothos::BlockRegistry::make("/blocks/binary_file_source", "int");
    fileSource.call("setFilePath", tempFile.path());

    auto fileSink = Pothos::BlockRegistry::make("/blocks/binary_file_sink");
    fileSink.call("setFilePath", tempFile.path());

    //create a test plan
    json testPlan;
    testPlan["enableBuffers"] = true;
    testPlan["minTrials"] = 100;
    testPlan["maxTrials"] = 200;
    testPlan["minSize"] = 512;
    testPlan["maxSize"] = 2048;
    auto expected = feeder.call("feedTestPlan", testPlan.dump());

    //run a topology that sends feeder to file
    {
        Pothos::Topology topology;
        topology.connect(feeder, 0, fileSink, 0);
        topology.commit();
        POTHOS_TEST_TRUE(topology.waitInactive());
    }

    //run a topology that sends file to collector
    {
        Pothos::Topology topology;
        topology.connect(fileSource, 0, collector, 0);
        topology.commit();
        POTHOS_TEST_TRUE(topology.waitInactive());
    }

    collector.call("verifyTestPlan", expected);
}

POTHOS_TEST_BLOCK("/blocks/tests", test_circular_binary_file_source)
{
    // Generate some random input.
    constexpr size_t maxSize = 2 << 15;

    Poco::Random rng;
    const size_t inputSize = rng.next(maxSize);

    Pothos::BufferChunk input("int", inputSize);

    Poco::RandomBuf randomBuf;
    randomBuf.readFromDevice(
        reinterpret_cast<char*>(input.address),
        (inputSize*sizeof(int)));

    auto feeder = Pothos::BlockRegistry::make("/blocks/feeder_source", "int");
    auto collector = Pothos::BlockRegistry::make("/blocks/collector_sink", "int");

    auto tempFile = Poco::TemporaryFile();
    POTHOS_TEST_TRUE(tempFile.createFile());

    auto fileSource = Pothos::BlockRegistry::make("/blocks/binary_file_source", "int");
    fileSource.call("setFilePath", tempFile.path());
    fileSource.call("setAutoRewind", true);

    auto fileSink = Pothos::BlockRegistry::make("/blocks/binary_file_sink");
    fileSink.call("setFilePath", tempFile.path());

    feeder.call("feedBuffer", input);

    // Run a topology that sends feeder to file
    {
        Pothos::Topology topology;
        topology.connect(feeder, 0, fileSink, 0);
        topology.commit();
        POTHOS_TEST_TRUE(topology.waitInactive());
    }

    // Run a topology that repeatedly sends file to collector
    {
        Pothos::Topology topology;
        topology.connect(fileSource, 0, collector, 0);
        topology.commit();

        Poco::Thread::sleep(10); // ms
    }

    // Check that the output is our file contents repeated
    auto output = collector.call<Pothos::BufferChunk>("getBuffer");
    POTHOS_TEST_TRUE(output.elements() >= input.elements());

    const auto numRepeats = output.elements() / input.elements();
    const int* inputPtr = reinterpret_cast<const int*>(input.address);
    const int* outputPtr = reinterpret_cast<const int*>(output.address);

    for(size_t i = 0; i < numRepeats; ++i)
    {
        POTHOS_TEST_EQUALA(
            inputPtr,
            outputPtr,
            inputSize);

        outputPtr += inputSize;
    }
}
