/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief Execute transaction context with coroutine
 * @file CoroutineTransactionExecutive.h
 * @author: jimmyshi
 * @date: 2022-07-19
 */

#pragma once

#include "SyncStorageWrapper.h"
#include "TransactionExecutive.h"
#include <boost/coroutine2/coroutine.hpp>

namespace bcos::executor
{

class CoroutineTransactionExecutive : public TransactionExecutive
{
public:
    using Ptr = std::shared_ptr<CoroutineTransactionExecutive>;


    class ResumeHandler;

    using CoroutineMessage = std::function<void(ResumeHandler resume)>;
    using Coroutine = boost::coroutines2::coroutine<CoroutineMessage>;

    class ResumeHandler
    {
    public:
        ResumeHandler(CoroutineTransactionExecutive& executive) : m_executive(executive) {}

        void operator()()
        {
            COROUTINE_TRACE_LOG(TRACE, m_executive.contextID(), m_executive.seq())
                << "Context switch to executive coroutine, from ResumeHandler";
            (*m_executive.m_pullMessage)();
        }

    private:
        CoroutineTransactionExecutive& m_executive;
    };


    CoroutineTransactionExecutive(const BlockContext& blockContext, std::string contractAddress,
        int64_t contextID, int64_t seq, const wasm::GasInjector& gasInjector)
      : TransactionExecutive(
            std::move(blockContext), std::move(contractAddress), contextID, seq, gasInjector),
        m_syncStorageWrapper(m_blockContext.storage(),
            [this](auto && PH1) { externalAcquireKeyLocks(std::forward<decltype(PH1)>(PH1)); },
            m_recoder)
    {
        m_storageWrapper = &m_syncStorageWrapper;
    }

    CallParameters::UniquePtr start(CallParameters::UniquePtr input) override;  // start a new
    // coroutine to
    // execute

    // External call request
    CallParameters::UniquePtr externalCall(CallParameters::UniquePtr input) override;  // call by
    // hostContext

    // External request key locks, throw exception if dead lock detected
    void externalAcquireKeyLocks(std::string acquireKeyLock);

    virtual void setExchangeMessage(CallParameters::UniquePtr callParameters)
    {
        m_exchangeMessage = std::move(callParameters);
    }

    std::string getExchangeMessageStr()
    {
        if (m_exchangeMessage)
        {
            return m_exchangeMessage->toString();
        }
        else
        {
            return "[empty exchange message]";
        }
    }


    virtual void appendResumeKeyLocks(std::vector<std::string> keyLocks)
    {
        std::copy(
            keyLocks.begin(), keyLocks.end(), std::back_inserter(m_exchangeMessage->keyLocks));
    }

    virtual CallParameters::UniquePtr resume()
    {
        EXECUTOR_LOG(TRACE) << "Context switch to executive coroutine, from resume";
        (*m_pullMessage)();

        return dispatcher();
    }

protected:
    CallParameters::UniquePtr m_exchangeMessage = nullptr;

private:
    CallParameters::UniquePtr dispatcher();
    void spawnAndCall(std::function<void(ResumeHandler)> function);

    SyncStorageWrapper m_syncStorageWrapper;

    std::optional<Coroutine::pull_type> m_pullMessage;
    std::optional<Coroutine::push_type> m_pushMessage;
};
}  // namespace bcos::executor
