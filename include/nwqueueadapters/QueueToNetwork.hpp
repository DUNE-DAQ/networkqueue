/**
 * @file QueueToNetwork.hpp
 *
 * Based on VectorIntIPMSenderDAQModule from IPM
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef NWQUEUEADAPTERS_INCLUDE_NWQUEUEADAPTERS_QUEUETONETWORK_HPP_
#define NWQUEUEADAPTERS_INCLUDE_NWQUEUEADAPTERS_QUEUETONETWORK_HPP_

#include "appfwk/DAQModule.hpp"
#include "appfwk/DAQSource.hpp"
#include "appfwk/ThreadHelper.hpp"

#include <ers/Issue.hpp>

#include <cetlib/BasicPluginFactory.h>
#include <cetlib/compiler_macros.h>

#include <memory>
#include <string>
#include <vector>

#include "appfwk/cmd/Nljs.hpp"

#include "nwqueueadapters/NetworkObjectSender.hpp"
#include "nwqueueadapters/Issues.hpp"

#include "serialization/Serialization.hpp"

#ifndef EXTERN_C_FUNC_DECLARE_START
#define EXTERN_C_FUNC_DECLARE_START                                                                                    \
  extern "C"                                                                                                           \
  {
#endif

#define MAKEQNIMPL(r, data, klass)                                       \
  if (plugin_name == BOOST_PP_STRINGIZE(klass))                         \
    return std::make_unique<dunedaq::nwqueueadapters::QueueToNetworkImpl<klass>>(queue_instance, sender_conf);
/**
 * @brief Declare the function that will be called by the plugin loader
 * @param klass Class for which a QueueToNetwork module will be used
 */
#define DEFINE_DUNE_QUEUE_TO_NETWORK(...)                                                                              \
  EXTERN_C_FUNC_DECLARE_START                                                                                          \
  std::unique_ptr<dunedaq::nwqueueadapters::QueueToNetworkBase> makeQToN(                                              \
    std::string const& plugin_name,                                                                                    \
    const std::string queue_instance,                                                                                  \
    const dunedaq::nwqueueadapters::networkobjectsender::Conf& sender_conf)                                            \
  {                                                                                                                    \
    BOOST_PP_SEQ_FOR_EACH(MAKEQNIMPL, , BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))                                         \
    return nullptr;                                                                                                    \
  }                                                                                                                    \
  }

namespace dunedaq::nwqueueadapters {

/**
 * @brief Base class for the queue-to-network implementation
 *
 * This class exists to perform a sort of "type erasure" for @c
 * QueueToNetwork: that class would more naturally
 * take a template parameter for the type of the queue it's reading
 * from (ie, @c QueueToNetwork<T>), but the plugin
 * factory we're using for instantiating @c DAQModules doesn't
 * support classes specialized on a template parameter. So instead,
 * @c QueueToNetwork holds an instance of @c
 * QueueToNetworkBase, and we push the "templateness" down to @c
 * QueueToNetworkImpl, which derives from @c QueueToNetworkBase.
 *
 * With this done, we still need to actually instantiate a
 * QueueToNetworkImpl<T> for the right value of T at runtime, and
 * that's done by codegen with moo (see
 * schema/nwqueueadapters-qtonmaker.hpp.j2) to produce a function that
 * will return an instance of the appropriate specialization of
 * QueueToNetworkImpl<T>, provided that T is one of the classes
 * generated by schema.
 */
class QueueToNetworkBase
{
public:
  virtual void get() = 0;
};

/**
 * @brief Implementation of QueueToNetworkBase that holds the "templateness"
 *
 * See documentation for QueueToNetworkBase for a (slightly) more complete description
 *
 */
template<typename MsgType>
class QueueToNetworkImpl : public QueueToNetworkBase
{
public:
  QueueToNetworkImpl(const std::string queue_instance,
                     const dunedaq::nwqueueadapters::networkobjectsender::Conf& sender_conf)
    : sender_(sender_conf)
  {
    inputQueue_.reset(new appfwk::DAQSource<MsgType>(queue_instance));
  }

  /**
   * @brief Pop an item off the queue and send it off to the network
   */
  virtual void get()
  {
    MsgType msg;
    inputQueue_->pop(msg, std::chrono::milliseconds(10));
    sender_.send(msg, std::chrono::milliseconds(10));
  }

private:
  std::unique_ptr<appfwk::DAQSource<MsgType>> inputQueue_;
  dunedaq::nwqueueadapters::NetworkObjectSender<MsgType> sender_;
};

/**
 * @brief QueueToNetwork connects a queue to an IPM sender, transparently to users of the queue
 *
 * This class is limited to types that are generated by schema in this
 * package. For reasons of plugin implementation, the type of objects
 * passed on the input queue is not specified as a template parameter,
 * but as the configuration parameter @c msg_type, which must be
 * specified in the init command of the module
 */
class QueueToNetwork : public appfwk::DAQModule
{
public:
  /**
   * @brief QueueToNetwork Constructor
   * @param name Instance name for this QueueToNetwork instance
   */
  explicit QueueToNetwork(const std::string& name);

  QueueToNetwork(const QueueToNetwork&) = delete;            ///< QueueToNetwork is not copy-constructible
  QueueToNetwork& operator=(const QueueToNetwork&) = delete; ///< QueueToNetwork is not copy-assignable
  QueueToNetwork(QueueToNetwork&&) = delete;                 ///< QueueToNetwork is not move-constructible
  QueueToNetwork& operator=(QueueToNetwork&&) = delete;      ///< QueueToNetwork is not move-assignable

  void init(const data_t&) override;

private:
  // Commands
  void do_configure(const data_t&);
  void do_scrap(const data_t&);

  // Threading
  void do_work(std::atomic<bool>& running_flag);
  appfwk::ThreadHelper thread_;

  std::string queue_instance_;
  std::string message_type_name_;
  std::unique_ptr<QueueToNetworkBase> impl_;
};

std::unique_ptr<QueueToNetworkBase>
makeQueueToNetworkBase(std::string const& module_name,
                       std::string const& plugin_name,
                       const std::string queue_instance,
                       const dunedaq::nwqueueadapters::networkobjectsender::Conf& sender_conf)
{
  static cet::BasicPluginFactory bpf("duneNetworkQueue", "makeQToN");
  auto ptr = bpf.makePlugin<std::unique_ptr<QueueToNetworkBase>>(module_name, plugin_name, queue_instance, sender_conf);
  if (!ptr) {
    throw NoQueueToNetworkImpl(ERS_HERE, plugin_name, module_name, queue_instance);
  }
  return ptr;
}

} // namespace dunedaq::nwqueueadapters

#endif // NWQUEUEADAPTERS_INCLUDE_NWQUEUEADAPTERS_QUEUETONETWORK_HPP_
