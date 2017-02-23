// Copyright 2016 Proyectos y Sistemas de Mantenimiento SL (eProsima).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * @file StatefulReader.cpp
 *
 */

#include <fastrtps/rtps/reader/StatefulReader.h>
#include <fastrtps/rtps/reader/WriterProxy.h>
#include <fastrtps/rtps/reader/ReaderListener.h>
#include <fastrtps/rtps/history/ReaderHistory.h>
#include <fastrtps/rtps/reader/timedevent/HeartbeatResponseDelay.h>
#include <fastrtps/rtps/reader/timedevent/InitialAckNack.h>
#include <fastrtps/log/Log.h>
#include <fastrtps/rtps/messages/RTPSMessageCreator.h>
#include "../participant/RTPSParticipantImpl.h"
#include "FragmentedChangePitStop.h"
#include <fastrtps/utils/TimeConversion.h>

#include <boost/thread/lock_guard.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/thread.hpp>

#include <cassert>

#define IDSTRING "(ID:"<< boost::this_thread::get_id() <<") "<<

using namespace eprosima::fastrtps::rtps;



StatefulReader::~StatefulReader()
{
    logInfo(RTPS_READER,"StatefulReader destructor.";);
    for(std::vector<WriterProxy*>::iterator it = matched_writers.begin();
            it!=matched_writers.end();++it)
    {
        delete(*it);
    }
}



StatefulReader::StatefulReader(RTPSParticipantImpl* pimpl,GUID_t& guid,
        ReaderAttributes& att,ReaderHistory* hist,ReaderListener* listen):
    RTPSReader(pimpl,guid,att,hist, listen),
    m_times(att.times)
{

}


bool StatefulReader::matched_writer_add(RemoteWriterAttributes& wdata)
{
    boost::lock_guard<boost::recursive_mutex> guard(*mp_mutex);
    for(std::vector<WriterProxy*>::iterator it=matched_writers.begin();
            it!=matched_writers.end();++it)
    {
        if((*it)->m_att.guid == wdata.guid)
        {
            logInfo(RTPS_READER,"Attempting to add existing writer");
            return false;
        }
    }
    WriterProxy* wp = new WriterProxy(wdata, this);

    wp->mp_initialAcknack->restart_timer();

    matched_writers.push_back(wp);
    logInfo(RTPS_READER,"Writer Proxy " <<wp->m_att.guid <<" added to " <<m_guid.entityId);
    return true;
}

bool StatefulReader::matched_writer_remove(RemoteWriterAttributes& wdata)
{
    WriterProxy *wproxy = nullptr;
    boost::unique_lock<boost::recursive_mutex> lock(*mp_mutex);

    //Remove cachechanges belonging to the unmatched writer
    mp_history->remove_changes_with_guid( &(wdata.guid) );

    for(std::vector<WriterProxy*>::iterator it=matched_writers.begin();it!=matched_writers.end();++it)
    {
        if((*it)->m_att.guid == wdata.guid)
        {
            logInfo(RTPS_READER,"Writer Proxy removed: " <<(*it)->m_att.guid);
            wproxy = *it;
            matched_writers.erase(it);
            break;
        }
    }

    lock.unlock();

        
    if(wproxy != nullptr)
    {
        delete wproxy;
        return true;
    }

    logInfo(RTPS_READER,"Writer Proxy " << wdata.guid << " doesn't exist in reader "<<this->getGuid().entityId);
    return false;
}

bool StatefulReader::matched_writer_remove(RemoteWriterAttributes& wdata,bool deleteWP)
{
    WriterProxy *wproxy = nullptr;
    boost::unique_lock<boost::recursive_mutex> lock(*mp_mutex);

    //Remove cachechanges belonging to the unmatched writer
    mp_history->remove_changes_with_guid( &(wdata.guid) );

    for(std::vector<WriterProxy*>::iterator it=matched_writers.begin();it!=matched_writers.end();++it)
    {
        if((*it)->m_att.guid == wdata.guid)
        {
            logInfo(RTPS_READER,"Writer Proxy removed: " <<(*it)->m_att.guid);
            wproxy = *it;
            matched_writers.erase(it);
            break;
        }
    }

    lock.unlock();

    if(wproxy != nullptr && deleteWP)
    {
        delete(wproxy);
        return true;
    }

    logInfo(RTPS_READER,"Writer Proxy " << wdata.guid << " doesn't exist in reader "<<this->getGuid().entityId);
    return false;
}

bool StatefulReader::matched_writer_is_matched(RemoteWriterAttributes& wdata)
{
    boost::lock_guard<boost::recursive_mutex> guard(*mp_mutex);
    for(std::vector<WriterProxy*>::iterator it=matched_writers.begin();it!=matched_writers.end();++it)
    {
        if((*it)->m_att.guid == wdata.guid)
        {
            return true;
        }
    }
    return false;
}


bool StatefulReader::matched_writer_lookup(const GUID_t& writerGUID, WriterProxy** WP)
{
    assert(WP);

    boost::lock_guard<boost::recursive_mutex> guard(*mp_mutex);

    bool returnedValue = findWriterProxy(writerGUID, WP);

    if(returnedValue)
    {
        logInfo(RTPS_READER,this->getGuid().entityId<<" FINDS writerProxy "<< writerGUID<<" from "<< matched_writers.size());
    }
    else
    {
        logInfo(RTPS_READER,this->getGuid().entityId<<" NOT FINDS writerProxy "<< writerGUID<<" from "<< matched_writers.size());
    }

    return returnedValue;
}

bool StatefulReader::findWriterProxy(const GUID_t& writerGUID, WriterProxy** WP)
{
    assert(WP);

    for(std::vector<WriterProxy*>::iterator it = matched_writers.begin(); it != matched_writers.end(); ++it)
    {
        if((*it)->m_att.guid == writerGUID)
        {
            *WP = *it;
            return true;
        }
    }
    return false;
}

bool StatefulReader::processDataMsg(CacheChange_t *change)
{
    WriterProxy *pWP = nullptr;

    assert(change);

    boost::unique_lock<boost::recursive_mutex> lock(*mp_mutex);

    if(acceptMsgFrom(change->writerGUID, &pWP))
    {
        logInfo(RTPS_MSG_IN,IDSTRING"Trying to add change " << change->sequenceNumber <<" TO reader: "<< getGuid().entityId);

        CacheChange_t* change_to_add;

        if(reserveCache(&change_to_add, change->serializedPayload.length)) //Reserve a new cache from the corresponding cache pool
        {
#if HAVE_SECURITY
            if(is_payload_protected())
            {
                change_to_add->copy_not_memcpy(change);
                if(!getRTPSParticipant()->security_manager().decode_serialized_payload(change->serializedPayload,
                        change_to_add->serializedPayload, m_guid, change->writerGUID))
                {
                    releaseCache(change_to_add);
                    logWarning(RTPS_MSG_IN, "Cannont decode serialized payload");
                    return false;
                }
            }
            else
            {
#endif
                if (!change_to_add->copy(change))
                {
                    logWarning(RTPS_MSG_IN,IDSTRING"Problem copying CacheChange, received data is: " << change->serializedPayload.length
                            << " bytes and max size in reader " << getGuid().entityId << " is " << change_to_add->serializedPayload.max_size);
                    releaseCache(change_to_add);
                    return false;
                }
#if HAVE_SECURITY
            }
#endif
        }
        else
        {
            logError(RTPS_MSG_IN,IDSTRING"Problem reserving CacheChange in reader: " << getGuid().entityId);
            return false;
        }

        // Assertion has to be done before call change_received,
        // because this function can unlock the StatefulReader mutex.
        if(pWP != nullptr)
        {
            pWP->assertLiveliness(); //Asser liveliness since you have received a DATA MESSAGE.
        }

        if(!change_received(change_to_add, pWP, lock))
        {
            logInfo(RTPS_MSG_IN,IDSTRING"MessageReceiver not add change "<<change_to_add->sequenceNumber);
            releaseCache(change_to_add);

            if(pWP == nullptr && getGuid().entityId == c_EntityId_SPDPReader)
            {
                mp_RTPSParticipant->assertRemoteRTPSParticipantLiveliness(change->writerGUID.guidPrefix);
            }
        }
    }

    return true;
}

bool StatefulReader::processDataFragMsg(CacheChange_t *incomingChange, uint32_t sampleSize, uint32_t fragmentStartingNum)
{
    WriterProxy *pWP = nullptr;

    assert(incomingChange);

    boost::unique_lock<boost::recursive_mutex> lock(*mp_mutex);

    if(acceptMsgFrom(incomingChange->writerGUID, &pWP))
    {
        // Check if CacheChange was received.
        if(!getHistory()->thereIsRecordOf(incomingChange->writerGUID, incomingChange->sequenceNumber))
        {
            logInfo(RTPS_MSG_IN, IDSTRING"Trying to add fragment " << incomingChange->sequenceNumber.to64long() << " TO reader: " << getGuid().entityId);

#if HAVE_SECURITY
            CacheChange_t* change_to_add = nullptr;

            if(is_payload_protected())
            {
                if(reserveCache(&change_to_add, incomingChange->serializedPayload.length)) //Reserve a new cache from the corresponding cache pool
                {
                    change_to_add->copy_not_memcpy(incomingChange);
                    if(!getRTPSParticipant()->security_manager().decode_serialized_payload(incomingChange->serializedPayload,
                                change_to_add->serializedPayload, m_guid, incomingChange->writerGUID))
                    {
                        releaseCache(change_to_add);
                        logWarning(RTPS_MSG_IN, "Cannont decode serialized payload");
                        return false;
                    }
                }
            }
            else
                change_to_add = incomingChange;
#endif

            // Fragments manager has to process incomming fragments.
            // If CacheChange_t is completed, it will be returned;
            CacheChange_t* change_completed = fragmentedChangePitStop_->process(change_to_add, sampleSize, fragmentStartingNum);

#if HAVE_SECURITY
            if(is_payload_protected())
                releaseCache(change_to_add);
#endif

            // Assertion has to be done before call change_received,
            // because this function can unlock the StatefulReader mutex.
            if(pWP != nullptr)
            {
                pWP->assertLiveliness(); //Asser liveliness since you have received a DATA MESSAGE.
            }

            if(change_completed != nullptr)
            {
                if(!change_received(change_completed, pWP, lock))
                {
                    logInfo(RTPS_MSG_IN, IDSTRING"MessageReceiver not add change " << change_completed->sequenceNumber.to64long());

                    // Assert liveliness because it is a participant discovery info.
                    if(pWP == nullptr && getGuid().entityId == c_EntityId_SPDPReader)
                    {
                        mp_RTPSParticipant->assertRemoteRTPSParticipantLiveliness(incomingChange->writerGUID.guidPrefix);
                    }

                    releaseCache(change_completed);
                }
            }
        }
    }

    return true;
}

bool StatefulReader::processHeartbeatMsg(GUID_t &writerGUID, uint32_t hbCount, SequenceNumber_t &firstSN,
            SequenceNumber_t &lastSN, bool finalFlag, bool livelinessFlag)
{
    WriterProxy *pWP = nullptr;

    boost::unique_lock<boost::recursive_mutex> lock(*mp_mutex);

    if(acceptMsgFrom(writerGUID, &pWP, false))
    {
        boost::unique_lock<boost::recursive_mutex> wpLock(*pWP->getMutex());

        if(pWP->m_lastHeartbeatCount < hbCount)
        {
            pWP->m_lastHeartbeatCount = hbCount;
            if(pWP->lost_changes_update(firstSN))
                fragmentedChangePitStop_->try_to_remove_until(firstSN, pWP->m_att.guid);
            pWP->missing_changes_update(lastSN);
            pWP->m_heartbeatFinalFlag = finalFlag;

            //Analyze wheter a acknack message is needed:
            if(!finalFlag)
            {
                pWP->mp_heartbeatResponse->restart_timer();
            }
            else if(finalFlag && !livelinessFlag)
            {
                if(pWP->areThereMissing())
                    pWP->mp_heartbeatResponse->restart_timer();
            }

            //FIXME: livelinessFlag
            if(livelinessFlag )//TODOG && WP->m_att->m_qos.m_liveliness.kind == MANUAL_BY_TOPIC_LIVELINESS_QOS)
            {
                pWP->assertLiveliness();
            }

            GUID_t proxGUID = pWP->m_att.guid;
            wpLock.unlock();

            // Maybe now we have to notify user from new CacheChanges.
            SequenceNumber_t nextChangeToNotify = pWP->nextCacheChangeToBeNotified();
            while(nextChangeToNotify != SequenceNumber_t::unknown())
            {
                if(getListener()!=nullptr)
                {
                    mp_history->postSemaphore();

                    CacheChange_t* ch_to_give = nullptr;
                    if(mp_history->get_change(nextChangeToNotify, proxGUID, &ch_to_give))
                    {
                        if(!ch_to_give->isRead)
                        {
                            lock.unlock();
                            getListener()->onNewCacheChangeAdded((RTPSReader*)this,ch_to_give);
                            lock.lock();
                        }
                    }

                    // Search again the WriterProxy because could be removed after the unlock.
                    if(!findWriterProxy(proxGUID, &pWP))
                        break;
                }

                nextChangeToNotify = pWP->nextCacheChangeToBeNotified();
            }
        }
    }

    return true;
}

bool StatefulReader::processGapMsg(GUID_t &writerGUID, SequenceNumber_t &gapStart, SequenceNumberSet_t &gapList)
{
    WriterProxy *pWP = nullptr;

    boost::unique_lock<boost::recursive_mutex> lock(*mp_mutex);

    if(acceptMsgFrom(writerGUID, &pWP, false))
    {
        boost::lock_guard<boost::recursive_mutex> guardWriterProxy(*pWP->getMutex());
        SequenceNumber_t auxSN;
        SequenceNumber_t finalSN = gapList.base -1;
        for(auxSN = gapStart; auxSN<=finalSN;auxSN++)
        {
            if(pWP->irrelevant_change_set(auxSN))
                fragmentedChangePitStop_->try_to_remove(auxSN, pWP->m_att.guid);
        }

        for(auto it = gapList.get_begin(); it != gapList.get_end();++it)
        {
            if(pWP->irrelevant_change_set((*it)))
                fragmentedChangePitStop_->try_to_remove((*it), pWP->m_att.guid);
        }
    }

    return true;
}

bool StatefulReader::acceptMsgFrom(GUID_t &writerId, WriterProxy **wp, bool checkTrusted)
{
    assert(wp != nullptr);

    if(checkTrusted && writerId.entityId == this->m_trustedWriterEntityId)
        return true;

    for(std::vector<WriterProxy*>::iterator it = this->matched_writers.begin();
            it!=matched_writers.end();++it)
    {
        if((*it)->m_att.guid == writerId)
        {
            *wp = *it;
            return true;
        }
    }

    return false;
}

bool StatefulReader::change_removed_by_history(CacheChange_t* a_change, WriterProxy* wp)
{
    boost::lock_guard<boost::recursive_mutex> guard(*mp_mutex);

    if(wp != nullptr || matched_writer_lookup(a_change->writerGUID,&wp))
    {
        wp->setNotValid(a_change->sequenceNumber);
        return true;
    }
    else
    {
        logError(RTPS_READER," You should always find the WP associated with a change, something is very wrong");
    }
    return false;
}

bool StatefulReader::change_received(CacheChange_t* a_change, WriterProxy* prox, boost::unique_lock<boost::recursive_mutex> &lock)
{


    //First look for WriterProxy in case is not provided
    if(prox == nullptr)
    {
        if(!findWriterProxy(a_change->writerGUID, &prox))
        {
            logInfo(RTPS_READER, "Writer Proxy " << a_change->writerGUID <<" not matched to this Reader "<< m_guid.entityId);
            return false;
        }
    }

    boost::unique_lock<boost::recursive_mutex> writerProxyLock(*prox->getMutex());

    size_t unknown_missing_changes_up_to = prox->unknown_missing_changes_up_to(a_change->sequenceNumber);

    // TODO Check order
    if(this->mp_history->received_change(a_change, unknown_missing_changes_up_to))
    {
        if(prox->received_change_set(a_change->sequenceNumber))
        {
            GUID_t proxGUID = prox->m_att.guid;
            writerProxyLock.unlock();

            SequenceNumber_t nextChangeToNotify = prox->nextCacheChangeToBeNotified();

            if(a_change->sequenceNumber == nextChangeToNotify)
            {
                mp_history->postSemaphore();

                if(getListener()!=nullptr)
                {
                    lock.unlock();
                    getListener()->onNewCacheChangeAdded((RTPSReader*)this,a_change);
                    lock.lock();

                    // Search again the WriterProxy because could be removed after the unlock.
                    if(!findWriterProxy(proxGUID, &prox))
                        return true;
                }

                nextChangeToNotify = prox->nextCacheChangeToBeNotified();
            }

            while(nextChangeToNotify != SequenceNumber_t::unknown())
            {
                mp_history->postSemaphore();

                if(getListener()!=nullptr)
                {
                    CacheChange_t* ch_to_give = nullptr;
                    if(mp_history->get_change(nextChangeToNotify, proxGUID, &ch_to_give))
                    {
                        if(!ch_to_give->isRead)
                        {
                            lock.unlock();
                            getListener()->onNewCacheChangeAdded((RTPSReader*)this,ch_to_give);
                            lock.lock();
                        }
                    }

                    // Search again the WriterProxy because could be removed after the unlock.
                    if(!findWriterProxy(proxGUID, &prox))
                        break;
                }

                nextChangeToNotify = prox->nextCacheChangeToBeNotified();
            }

            return true;
        }
    }

    return false;
}

bool StatefulReader::nextUntakenCache(CacheChange_t** change,WriterProxy** wpout)
{
    boost::lock_guard<boost::recursive_mutex> guard(*mp_mutex);
    std::vector<CacheChange_t*> toremove;
    bool takeok = false;
    for(std::vector<CacheChange_t*>::iterator it = mp_history->changesBegin();
            it!=mp_history->changesEnd();++it)
    {
        WriterProxy* wp;
        if(this->matched_writer_lookup((*it)->writerGUID, &wp))
        {
            // TODO Revisar la comprobacion
            SequenceNumber_t seq = wp->available_changes_max();
            if(seq >= (*it)->sequenceNumber)
            {
                *change = *it;
                if(wpout !=nullptr)
                    *wpout = wp;

                takeok = true;
                break;
                //				if((*it)->kind == ALIVE)
                //				{
                //					this->mp_type->deserialize(&(*it)->serializedPayload,data);
                //				}
                //				(*it)->isRead = true;
                //				if(info!=NULL)
                //				{
                //					info->sampleKind = (*it)->kind;
                //					info->writerGUID = (*it)->writerGUID;
                //					info->sourceTimestamp = (*it)->sourceTimestamp;
                //					info->iHandle = (*it)->instanceHandle;
                //					if(this->m_qos.m_ownership.kind == EXCLUSIVE_OWNERSHIP_QOS)
                //						info->ownershipStrength = wp->m_data->m_qos.m_ownershipStrength.value;
                //				}
                //				m_reader_cache.decreaseUnreadCount();
                //				logInfo(RTPS_READER,this->getGuid().entityId<<": reading change "<< (*it)->sequenceNumber.to64long());
                //				readok = true;
                //				break;
            }
        }
        else
        {
            toremove.push_back((*it));
        }
    }

    for(std::vector<CacheChange_t*>::iterator it = toremove.begin();
            it!=toremove.end();++it)
    {
        logWarning(RTPS_READER,"Removing change "<<(*it)->sequenceNumber << " from " << (*it)->writerGUID << " because is no longer paired");
        mp_history->remove_change(*it);
    }
    return takeok;
}

// TODO Porque elimina aqui y no cuando hay unpairing
bool StatefulReader::nextUnreadCache(CacheChange_t** change,WriterProxy** wpout)
{
    boost::lock_guard<boost::recursive_mutex> guard(*mp_mutex);
    std::vector<CacheChange_t*> toremove;
    bool readok = false;
    for(std::vector<CacheChange_t*>::iterator it = mp_history->changesBegin();
            it!=mp_history->changesEnd();++it)
    {
        if((*it)->isRead)
            continue;
        WriterProxy* wp;
        if(this->matched_writer_lookup((*it)->writerGUID,&wp))
        {
            SequenceNumber_t seq;
            seq = wp->available_changes_max();
            if(seq >= (*it)->sequenceNumber)
            {
                *change = *it;
                if(wpout !=nullptr)
                    *wpout = wp;

                readok = true;
                break;
                //				if((*it)->kind == ALIVE)
                //				{
                //					this->mp_type->deserialize(&(*it)->serializedPayload,data);
                //				}
                //				(*it)->isRead = true;
                //				if(info!=NULL)
                //				{
                //					info->sampleKind = (*it)->kind;
                //					info->writerGUID = (*it)->writerGUID;
                //					info->sourceTimestamp = (*it)->sourceTimestamp;
                //					info->iHandle = (*it)->instanceHandle;
                //					if(this->m_qos.m_ownership.kind == EXCLUSIVE_OWNERSHIP_QOS)
                //						info->ownershipStrength = wp->m_data->m_qos.m_ownershipStrength.value;
                //				}
                //				m_reader_cache.decreaseUnreadCount();
                //				logInfo(RTPS_READER,this->getGuid().entityId<<": reading change "<< (*it)->sequenceNumber.to64long());
                //				readok = true;
                //				break;
            }
        }
        else
        {
            toremove.push_back((*it));
        }
    }

    for(std::vector<CacheChange_t*>::iterator it = toremove.begin();
            it!=toremove.end();++it)
    {
        logWarning(RTPS_READER,"Removing change "<<(*it)->sequenceNumber << " from " << (*it)->writerGUID << " because is no longer paired");
        mp_history->remove_change(*it);
    }

    return readok;
}

//
//bool StatefulReader::acceptMsgFrom(GUID_t& writerId,WriterProxy** wp)
//{
//	if(this->m_acceptMessagesFromUnkownWriters)
//	{
//		for(std::vector<WriterProxy*>::iterator it = this->matched_writers.begin();
//				it!=matched_writers.end();++it)
//		{
//			if((*it)->m_data->m_guid == writerId)
//			{
//				if(wp!=NULL)
//					*wp = *it;
//				return true;
//			}
//		}
//	}
//	else
//	{
//		if(writerId.entityId == this->m_trustedWriterEntityId)
//			return true;
//	}
//	return false;
//}
//
bool StatefulReader::updateTimes(ReaderTimes& ti)
{
    boost::lock_guard<boost::recursive_mutex> guard(*mp_mutex);
    if(m_times.heartbeatResponseDelay != ti.heartbeatResponseDelay)
    {
        m_times = ti;
        for(std::vector<WriterProxy*>::iterator wit = this->matched_writers.begin();
                wit!=this->matched_writers.end();++wit)
        {
            (*wit)->mp_heartbeatResponse->update_interval(m_times.heartbeatResponseDelay);
        }
    }
    return true;
}

bool StatefulReader::isInCleanState() const
{
    bool cleanState = true;
    boost::unique_lock<boost::recursive_mutex> lock(*mp_mutex);

    for (WriterProxy* wp : matched_writers)
    {
        if (wp->numberOfChangeFromWriter() != 0)
        {
            cleanState = false;
            break;
        }
    }

    return cleanState;
}
