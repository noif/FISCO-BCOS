#include "../bcos-transaction-executor/precompiled/PrecompiledManager.h"
#include "../bcos-transaction-executor/vm/HostContext.h"
#include "../bcos-transaction-executor/vm/VMInstance.h"
#include "TestBytecode.h"
#include "bcos-codec/bcos-codec/abi/ContractABICodec.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-transaction-executor/RollbackableStorage.h"
#include "bcos-transaction-executor/vm/VMFactory.h"
#include "bcos-utilities/FixedBytes.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-task/Wait.h>
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <iterator>
#include <memory>

using namespace bcos::task;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;

class TestHostContextFixture
{
public:
    TestHostContextFixture() : rollbackableStorage(storage)
    {
        bcos::transaction_executor::GlobalHashImpl::g_hashImpl =
            std::make_shared<bcos::crypto::Keccak256>();
        bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
        precompiledManager.emplace(std::make_shared<bcos::crypto::Keccak256>());

        // deploy the hello world contract
        syncWait([this]() -> Task<void> {
            bcostars::protocol::BlockHeaderImpl blockHeader(
                [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });

            std::string helloworldBytecodeBinary;
            boost::algorithm::unhex(
                helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));

            evmc_message message = {.kind = EVMC_CREATE,
                .flags = 0,
                .depth = 0,
                .gas = 300 * 10000,
                .recipient = {},
                .destination_ptr = nullptr,
                .destination_len = 0,
                .sender = {},
                .sender_ptr = nullptr,
                .sender_len = 0,
                .input_data = (const uint8_t*)helloworldBytecodeBinary.data(),
                .input_size = helloworldBytecodeBinary.size(),
                .value = {},
                .create2_salt = {},
                .code_address = {}};
            evmc_address origin = {};

            HostContext hostContext(vmFactory, rollbackableStorage, blockHeader, message, origin, 0,
                seq, *precompiledManager);
            auto result = co_await hostContext.execute();

            BOOST_REQUIRE_EQUAL(result.status_code, 0);

            helloworldAddress = result.create_address;
        }());
    }

    template <class... Arg>
    Task<EVMCResult> call(std::string_view abi, Arg const&... args)
    {
        bcos::codec::abi::ContractABICodec abiCodec(
            bcos::transaction_executor::GlobalHashImpl::g_hashImpl);
        auto input = abiCodec.abiIn(std::string(abi), args...);

        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = 1000000,
            .recipient = helloworldAddress,
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = {},
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = input.data(),
            .input_size = input.size(),
            .value = {},
            .create2_salt = {},
            .code_address = helloworldAddress};
        evmc_address origin = {};

        HostContext hostContext(vmFactory, rollbackableStorage, blockHeader, message, origin, 0,
            seq, *precompiledManager);
        auto result = co_await hostContext.execute();

        co_return result;
    }

    memory_storage::MemoryStorage<StateKey, StateValue, memory_storage::ORDERED> storage;
    Rollbackable<decltype(storage)> rollbackableStorage;
    evmc_address helloworldAddress;
    VMFactory vmFactory;
    int64_t seq = 0;
    std::optional<PrecompiledManager> precompiledManager;
};

bcos::crypto::Hash::Ptr bcos::transaction_executor::GlobalHashImpl::g_hashImpl;

BOOST_FIXTURE_TEST_SUITE(TestHostContext, TestHostContextFixture)

BOOST_AUTO_TEST_CASE(simpleCall)
{
    syncWait([this]() -> Task<void> {
        auto result = co_await call(std::string("getInt()"));

        BOOST_CHECK_EQUAL(result.status_code, 0);
        bcos::s256 getIntResult = -1;
        bcos::codec::abi::ContractABICodec abiCodec(
            bcos::transaction_executor::GlobalHashImpl::g_hashImpl);
        abiCodec.abiOut(bcos::bytesConstRef(result.output_data, result.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 0);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(executeAndCall)
{
    syncWait([this]() -> Task<void> {
        auto result1 = co_await call("setInt(int256)", bcos::s256(10000));
        auto result2 = co_await call("getInt()");
        auto result3 = co_await call("setString(string)", std::string("Hello world, fisco-bcos!"));
        auto result4 = co_await call("getString()");

        BOOST_CHECK_EQUAL(result1.status_code, 0);
        BOOST_CHECK_EQUAL(result2.status_code, 0);
        BOOST_CHECK_EQUAL(result3.status_code, 0);
        BOOST_CHECK_EQUAL(result4.status_code, 0);
        bcos::s256 getIntResult = -1;
        bcos::codec::abi::ContractABICodec abiCodec(
            bcos::transaction_executor::GlobalHashImpl::g_hashImpl);
        abiCodec.abiOut(
            bcos::bytesConstRef(result2.output_data, result2.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 10000);

        std::string out;
        abiCodec.abiOut(bcos::bytesConstRef(result4.output_data, result4.output_size), out);
        BOOST_CHECK_EQUAL(out, "Hello world, fisco-bcos!");

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(contractDeploy)
{
    syncWait([this]() -> Task<void> {
        auto result = co_await call("deployAndCall(int256)", bcos::s256(999));

        BOOST_CHECK_EQUAL(result.status_code, 0);
        bcos::s256 getIntResult = -1;
        bcos::codec::abi::ContractABICodec abiCodec(
            bcos::transaction_executor::GlobalHashImpl::g_hashImpl);
        abiCodec.abiOut(bcos::bytesConstRef(result.output_data, result.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 999);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(createTwice)
{
    syncWait([this]() -> Task<void> {
        auto result = co_await call("createTwice()");
        BOOST_CHECK_EQUAL(result.status_code, 0);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(failure)
{
    syncWait([this]() -> Task<void> {
        bcos::codec::abi::ContractABICodec abiCodec(
            bcos::transaction_executor::GlobalHashImpl::g_hashImpl);

        auto result1 = co_await call("returnRequire()");
        BOOST_CHECK_EQUAL(result1.status_code, 2);

        auto result2 = co_await call("getInt()");
        BOOST_CHECK_EQUAL(result2.status_code, 0);
        bcos::s256 getIntResult = -1;
        abiCodec.abiOut(
            bcos::bytesConstRef(result2.output_data, result2.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 0);

        auto result3 = co_await call("returnRevert()");
        BOOST_CHECK_EQUAL(result3.status_code, 2);

        auto result4 = co_await call("getInt()");
        BOOST_CHECK_EQUAL(result4.status_code, 0);
        abiCodec.abiOut(
            bcos::bytesConstRef(result4.output_data, result4.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 0);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(delegateCall)
{
    syncWait([this]() -> Task<void> {
        bcos::codec::abi::ContractABICodec abiCodec(
            bcos::transaction_executor::GlobalHashImpl::g_hashImpl);

        auto result1 = co_await call("delegateCall()");
        BOOST_CHECK_EQUAL(result1.status_code, 0);

        auto result2 = co_await call("getInt()");
        bcos::s256 getIntResult = -1;
        abiCodec.abiOut(
            bcos::bytesConstRef(result2.output_data, result2.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 19876);

        auto result3 = co_await call("getString()");
        std::string strResult;
        abiCodec.abiOut(bcos::bytesConstRef(result3.output_data, result3.output_size), strResult);
        BOOST_CHECK_EQUAL(strResult, "hi!");
    }());
}

BOOST_AUTO_TEST_CASE(log)
{
    // syncWait([this]() -> Task<void> {
    // auto result1 = co_await call("setInt(int256)", bcos::s256(10000));
    // auto result2 = co_await call("setString(string)", std::string("Hello world,
    // fisco-bcos!")); auto result3 = co_await call("logOut()");

    // BOOST_CHECK_EQUAL(result1.status_code, 0);
    // BOOST_CHECK_EQUAL(result2.status_code, 0);
    // BOOST_CHECK_EQUAL(result3.status_code, 0);

    // bcos::s256 getIntResult = -1;
    // std::string out;
    // bcos::codec::abi::ContractABICodec abiCodec(
    //     bcos::transaction_executor::GlobalHashImpl::g_hashImpl);

    // abiCodec.abiOut(bcos::bytesConstRef(.output_data, result4.output_size), out);
    // BOOST_CHECK_EQUAL(out, "Hello world, fisco-bcos!");

    // releaseResult(result1);
    // releaseResult(result2);

    //     co_return;
    // }());
}

#if 0
BOOST_AUTO_TEST_CASE(precompiled)
{
    syncWait([this]() -> Task<void> {
        bcos::codec::abi::ContractABICodec abiCodec(
            bcos::transaction_executor::GlobalHashImpl::g_hashImpl);
        auto input = abiCodec.abiIn(std::string("makeShard(string)"), std::string("shared1"));

        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        blockHeader.mutableInner().data.version = (int)bcos::protocol::BlockVersion::V3_5_VERSION;
        blockHeader.calculateHash(*bcos::transaction_executor::GlobalHashImpl::g_hashImpl);

        auto address = bcos::Address(0x1010);
        evmc_address callAddress{};
        std::uninitialized_copy(address.begin(), address.end(), callAddress.bytes);
        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = 1000000,
            .recipient = callAddress,
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = {},
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = input.data(),
            .input_size = input.size(),
            .value = {},
            .create2_salt = {},
            .code_address = callAddress};
        evmc_address origin = {};

        HostContext hostContext(vmFactory, rollbackableStorage, blockHeader, message, origin, 0,
            seq, *precompiledManager);
        auto result = co_await hostContext.execute();

        BOOST_CHECK_EQUAL(result.status_code, 0);
        bcos::s256 getIntResult = -1;
        abiCodec.abiOut(bcos::bytesConstRef(result.output_data, result.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 0);

        co_return;
    }());
}
#endif

BOOST_AUTO_TEST_SUITE_END()