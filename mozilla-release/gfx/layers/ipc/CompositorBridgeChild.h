/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_CompositorBridgeChild_h
#define mozilla_layers_CompositorBridgeChild_h

#include "base/basictypes.h"            // for DISALLOW_EVIL_CONSTRUCTORS
#include "mozilla/Assertions.h"         // for MOZ_ASSERT_HELPER2
#include "mozilla/Attributes.h"         // for override
#include "mozilla/Monitor.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/layers/PCompositorBridgeChild.h"
#include "mozilla/layers/TextureForwarder.h" // for TextureForwarder
#include "mozilla/webrender/WebRenderTypes.h"
#include "nsClassHashtable.h"           // for nsClassHashtable
#include "nsRefPtrHashtable.h"
#include "nsCOMPtr.h"                   // for nsCOMPtr
#include "nsHashKeys.h"                 // for nsUint64HashKey
#include "nsISupportsImpl.h"            // for NS_INLINE_DECL_REFCOUNTING
#include "ThreadSafeRefcountingWithMainThreadDestruction.h"
#include "nsWeakReference.h"

namespace mozilla {

namespace dom {
class TabChild;
} // namespace dom

namespace widget {
class CompositorWidget;
} // namespace widget

namespace layers {

using mozilla::dom::TabChild;

class IAPZCTreeManager;
class APZCTreeManagerChild;
class ClientLayerManager;
class CompositorBridgeParent;
class CompositorManagerChild;
class CompositorOptions;
class TextureClient;
class TextureClientPool;
class CapturedPaintState;
struct FrameMetrics;

class CompositorBridgeChild final : public PCompositorBridgeChild,
                                    public TextureForwarder
{
  typedef InfallibleTArray<AsyncParentMessageData> AsyncParentMessageArray;

public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CompositorBridgeChild, override);

  explicit CompositorBridgeChild(CompositorManagerChild* aManager);

  /**
   * Initialize the singleton compositor bridge for a content process.
   */
  void InitForContent(uint32_t aNamespace);

  void InitForWidget(uint64_t aProcessToken,
                     LayerManager* aLayerManager,
                     uint32_t aNamespace);

  void Destroy();

  /**
   * Lookup the FrameMetrics shared by the compositor process with the
   * associated FrameMetrics::ViewID. The returned FrameMetrics is used
   * in progressive paint calculations.
   */
  bool LookupCompositorFrameMetrics(const FrameMetrics::ViewID aId, FrameMetrics&);

  static CompositorBridgeChild* Get();

  static bool ChildProcessHasCompositorBridge();

  // Returns whether the compositor is in the GPU process (false if in the UI
  // process). This may only be called on the main thread.
  static bool CompositorIsInGPUProcess();

  virtual mozilla::ipc::IPCResult
  RecvDidComposite(const uint64_t& aId, const uint64_t& aTransactionId,
                   const TimeStamp& aCompositeStart,
                   const TimeStamp& aCompositeEnd) override;

  virtual mozilla::ipc::IPCResult
  RecvInvalidateLayers(const uint64_t& aLayersId) override;

  virtual mozilla::ipc::IPCResult
  RecvUpdatePluginConfigurations(const LayoutDeviceIntPoint& aContentOffset,
                                 const LayoutDeviceIntRegion& aVisibleRegion,
                                 nsTArray<PluginWindowData>&& aPlugins) override;

  virtual mozilla::ipc::IPCResult
  RecvCaptureAllPlugins(const uintptr_t& aParentWidget) override;

  virtual mozilla::ipc::IPCResult
  RecvHideAllPlugins(const uintptr_t& aParentWidget) override;

  virtual PTextureChild* AllocPTextureChild(const SurfaceDescriptor& aSharedData,
                                            const LayersBackend& aLayersBackend,
                                            const TextureFlags& aFlags,
                                            const uint64_t& aId,
                                            const uint64_t& aSerial,
                                            const wr::MaybeExternalImageId& aExternalImageId) override;

  virtual bool DeallocPTextureChild(PTextureChild* actor) override;

  virtual mozilla::ipc::IPCResult
  RecvParentAsyncMessages(InfallibleTArray<AsyncParentMessageData>&& aMessages) override;
  virtual PTextureChild* CreateTexture(const SurfaceDescriptor& aSharedData,
                                       LayersBackend aLayersBackend,
                                       TextureFlags aFlags,
                                       uint64_t aSerial,
                                       wr::MaybeExternalImageId& aExternalImageId,
                                       nsIEventTarget* aTarget) override;

  /**
   * Request that the parent tell us when graphics are ready on GPU.
   * When we get that message, we bounce it to the TabParent via
   * the TabChild
   * @param tabChild The object to bounce the note to.  Non-NULL.
   */
  void RequestNotifyAfterRemotePaint(TabChild* aTabChild);

  void CancelNotifyAfterRemotePaint(TabChild* aTabChild);

  // Beware that these methods don't override their super-class equivalent (which
  // are not virtual), they just overload them.
  // All of these Send* methods just add a sanity check (that it is not too late
  // send a message) and forward the call to the super-class's equivalent method.
  // This means that it is correct to call directly the super-class methods, but
  // you won't get the extra safety provided here.
  bool SendWillClose();
  bool SendPause();
  bool SendResume();
  bool SendNotifyChildCreated(const uint64_t& id, CompositorOptions* aOptions);
  bool SendAdoptChild(const uint64_t& id);
  bool SendMakeSnapshot(const SurfaceDescriptor& inSnapshot, const gfx::IntRect& dirtyRect);
  bool SendFlushRendering();
  bool SendGetTileSize(int32_t* tileWidth, int32_t* tileHeight);
  bool SendStartFrameTimeRecording(const int32_t& bufferSize, uint32_t* startIndex);
  bool SendStopFrameTimeRecording(const uint32_t& startIndex, nsTArray<float>* intervals);
  bool SendNotifyRegionInvalidated(const nsIntRegion& region);
  bool SendRequestNotifyAfterRemotePaint();
  bool SendClearApproximatelyVisibleRegions(uint64_t aLayersId, uint32_t aPresShellId);
  bool SendNotifyApproximatelyVisibleRegion(const ScrollableLayerGuid& aGuid,
                                            const mozilla::CSSIntRegion& aRegion);
  bool SendAllPluginsCaptured();
  bool IsSameProcess() const override;

  virtual bool IPCOpen() const override { return mCanSend; }

  static void ShutDown();

  void UpdateFwdTransactionId() { ++mFwdTransactionId; }
  uint64_t GetFwdTransactionId() { return mFwdTransactionId; }

  /**
   * Hold TextureClient ref until end of usage on host side if TextureFlags::RECYCLE is set.
   * Host side's usage is checked via CompositableRef.
   */
  void HoldUntilCompositableRefReleasedIfNecessary(TextureClient* aClient);

  /**
   * Notify id of Texture When host side end its use. Transaction id is used to
   * make sure if there is no newer usage.
   */
  void NotifyNotUsed(uint64_t aTextureId, uint64_t aFwdTransactionId);

  virtual void CancelWaitForRecycle(uint64_t aTextureId) override;

  TextureClientPool* GetTexturePool(KnowsCompositor* aAllocator,
                                    gfx::SurfaceFormat aFormat,
                                    TextureFlags aFlags);
  void ClearTexturePool();

  virtual FixedSizeSmallShmemSectionAllocator* GetTileLockAllocator() override;

  void HandleMemoryPressure();

  virtual MessageLoop* GetMessageLoop() const override { return mMessageLoop; }

  virtual base::ProcessId GetParentPid() const override { return OtherPid(); }

  virtual bool AllocUnsafeShmem(size_t aSize,
                                mozilla::ipc::SharedMemory::SharedMemoryType aShmType,
                                mozilla::ipc::Shmem* aShmem) override;
  virtual bool AllocShmem(size_t aSize,
                          mozilla::ipc::SharedMemory::SharedMemoryType aShmType,
                          mozilla::ipc::Shmem* aShmem) override;
  virtual bool DeallocShmem(mozilla::ipc::Shmem& aShmem) override;

  PCompositorWidgetChild* AllocPCompositorWidgetChild(const CompositorWidgetInitData& aInitData) override;
  bool DeallocPCompositorWidgetChild(PCompositorWidgetChild* aActor) override;

  PAPZCTreeManagerChild* AllocPAPZCTreeManagerChild(const uint64_t& aLayersId) override;
  bool DeallocPAPZCTreeManagerChild(PAPZCTreeManagerChild* aActor) override;

  PAPZChild* AllocPAPZChild(const uint64_t& aLayersId) override;
  bool DeallocPAPZChild(PAPZChild* aActor) override;

  void WillEndTransaction();

  PWebRenderBridgeChild* AllocPWebRenderBridgeChild(const wr::PipelineId& aPipelineId,
                                                    const LayoutDeviceIntSize&,
                                                    TextureFactoryIdentifier*,
                                                    wr::IdNamespace*) override;
  bool DeallocPWebRenderBridgeChild(PWebRenderBridgeChild* aActor) override;

  uint64_t DeviceResetSequenceNumber() const {
    return mDeviceResetSequenceNumber;
  }

  wr::MaybeExternalImageId GetNextExternalImageId() override;

  wr::PipelineId GetNextPipelineId();

  // Must only be called from the main thread. Ensures that any paints from
  // previous frames have been flushed. The main thread blocks until the
  // operation completes.
  void FlushAsyncPaints();

  // Must only be called from the main thread. Notifies the CompositorBridge
  // that the paint thread is going to begin painting asynchronously.
  void NotifyBeginAsyncPaint(CapturedPaintState* aState);

  // Must only be called from the paint thread. Notifies the CompositorBridge
  // that the paint thread has finished an asynchronous paint request.
  void NotifyFinishedAsyncPaint(CapturedPaintState* aState);

  // Must only be called from the main thread. Notifies the CompositorBridge
  // that the paint thread is going to perform texture synchronization at the
  // end of async painting, and should postpone messages if needed until
  // finished.
  void NotifyBeginAsyncEndLayerTransaction();

  // Must only be called from the paint thread. Notifies the CompositorBridge
  // that the paint thread has finished all async paints and texture syncs from
  // a given transaction and may resume sending messages.
  void NotifyFinishedAsyncEndLayerTransaction();

  // Must only be called from the main thread. Notifies the CompoistorBridge
  // that a transaction is about to be sent, and if the paint thread is
  // currently painting, to begin delaying IPC messages.
  void PostponeMessagesIfAsyncPainting();

private:
  // Private destructor, to discourage deletion outside of Release():
  virtual ~CompositorBridgeChild();

  // Must only be called from the paint thread. If the main thread is delaying
  // IPC messages, this forwards all such delayed IPC messages to the I/O thread
  // and resumes IPC.
  void ResumeIPCAfterAsyncPaint();

  void AfterDestroy();

  virtual PLayerTransactionChild*
    AllocPLayerTransactionChild(const nsTArray<LayersBackend>& aBackendHints,
                                const uint64_t& aId) override;

  virtual bool DeallocPLayerTransactionChild(PLayerTransactionChild *aChild) override;

  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

  virtual mozilla::ipc::IPCResult RecvSharedCompositorFrameMetrics(const mozilla::ipc::SharedMemoryBasic::Handle& metrics,
                                                                   const CrossProcessMutexHandle& handle,
                                                                   const uint64_t& aLayersId,
                                                                   const uint32_t& aAPZCId) override;

  virtual mozilla::ipc::IPCResult RecvReleaseSharedCompositorFrameMetrics(const ViewID& aId,
                                                                          const uint32_t& aAPZCId) override;

  virtual mozilla::ipc::IPCResult
  RecvRemotePaintIsReady() override;

  mozilla::ipc::IPCResult RecvObserveLayerUpdate(const uint64_t& aLayersId,
                                                 const uint64_t& aEpoch,
                                                 const bool& aActive) override;

  virtual mozilla::ipc::IPCResult
  RecvNotifyWebRenderError(const WebRenderError& aError) override;

  uint64_t GetNextResourceId();

  // Class used to store the shared FrameMetrics, mutex, and APZCId  in a hash table
  class SharedFrameMetricsData {
  public:
    SharedFrameMetricsData(
        const mozilla::ipc::SharedMemoryBasic::Handle& metrics,
        const CrossProcessMutexHandle& handle,
        const uint64_t& aLayersId,
        const uint32_t& aAPZCId);

    ~SharedFrameMetricsData();

    void CopyFrameMetrics(FrameMetrics* aFrame);
    FrameMetrics::ViewID GetViewID();
    uint64_t GetLayersId() const;
    uint32_t GetAPZCId();

  private:
    // Pointer to the class that allows access to the shared memory that contains
    // the shared FrameMetrics
    RefPtr<mozilla::ipc::SharedMemoryBasic> mBuffer;
    CrossProcessMutex* mMutex;
    uint64_t mLayersId;
    // Unique ID of the APZC that is sharing the FrameMetrics
    uint32_t mAPZCId;
  };

  RefPtr<CompositorManagerChild> mCompositorManager;

  RefPtr<LayerManager> mLayerManager;

  uint32_t mIdNamespace;
  uint32_t mResourceId;

  // When not multi-process, hold a reference to the CompositorBridgeParent to keep it
  // alive. This reference should be null in multi-process.
  RefPtr<CompositorBridgeParent> mCompositorBridgeParent;

  // The ViewID of the FrameMetrics is used as the key for this hash table.
  // While this should be safe to use since the ViewID is unique
  nsClassHashtable<nsUint64HashKey, SharedFrameMetricsData> mFrameMetricsTable;

  // Weakly hold the TabChild that made a request to be alerted when
  // the transaction has been received.
  nsWeakPtr mWeakTabChild;      // type is TabChild

  DISALLOW_EVIL_CONSTRUCTORS(CompositorBridgeChild);

  // True until the beginning of the two-step shutdown sequence of this actor.
  bool mCanSend;

  // False until the actor is destroyed.
  bool mActorDestroyed;

  /**
   * Transaction id of ShadowLayerForwarder.
   * It is incrementaed by UpdateFwdTransactionId() in each BeginTransaction() call.
   */
  uint64_t mFwdTransactionId;

  /**
   * Last sequence number recognized for a device reset.
   */
  uint64_t mDeviceResetSequenceNumber;

  /**
   * Hold TextureClients refs until end of their usages on host side.
   * It defer calling of TextureClient recycle callback.
   */
  nsRefPtrHashtable<nsUint64HashKey, TextureClient> mTexturesWaitingRecycled;

  MessageLoop* mMessageLoop;

  AutoTArray<RefPtr<TextureClientPool>,2> mTexturePools;

  uint64_t mProcessToken;

  FixedSizeSmallShmemSectionAllocator* mSectionAllocator;

  // TextureClients that must be kept alive during async painting. This
  // is only accessed on the main thread.
  nsTArray<RefPtr<TextureClient>> mTextureClientsForAsyncPaint;

  // Off-Main-Thread Painting state. This covers access to the OMTP-related
  // state below.
  Monitor mPaintLock;

  // Contains the number of outstanding asynchronous paints tied to a
  // PLayerTransaction on this bridge. This is R/W on both the main and paint
  // threads, and must be accessed within the paint lock.
  size_t mOutstandingAsyncPaints;

  // Whether we are waiting for an async paint end transaction
  bool mOutstandingAsyncEndTransaction;

  // True if this CompositorBridge is currently delaying its messages until the
  // paint thread completes. This is R/W on both the main and paint threads, and
  // must be accessed within the paint lock.
  bool mIsWaitingForPaint;
};

} // namespace layers
} // namespace mozilla

#endif // mozilla_layers_CompositorBrigedChild_h