/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=8 et :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_ShadowLayers_h
#define mozilla_layers_ShadowLayers_h 1

#include <stddef.h>                     // for size_t
#include <stdint.h>                     // for uint64_t
#include "gfxTypes.h"
#include "mozilla/Attributes.h"         // for override
#include "mozilla/gfx/Rect.h"
#include "mozilla/WidgetUtils.h"        // for ScreenRotation
#include "mozilla/dom/ScreenOrientation.h"  // for ScreenOrientation
#include "mozilla/ipc/SharedMemory.h"   // for SharedMemory, etc
#include "mozilla/layers/CompositableForwarder.h"
#include "mozilla/layers/CompositorTypes.h"  // for OpenMode, etc
#include "nsCOMPtr.h"                   // for already_AddRefed
#include "nsRegion.h"                   // for nsIntRegion
#include "nsTArrayForwardDeclare.h"     // for InfallibleTArray
#include "nsIWidget.h"
#include <vector>

namespace mozilla {
namespace layers {

class EditReply;
class FixedSizeSmallShmemSectionAllocator;
class ImageContainer;
class Layer;
class PLayerChild;
class PLayerTransactionChild;
class LayerTransactionChild;
class ShadowableLayer;
class SurfaceDescriptor;
class TextureClient;
class ThebesBuffer;
class ThebesBufferData;
class Transaction;

/**
 * We want to share layer trees across thread contexts and address
 * spaces for several reasons; chief among them
 *
 *  - a parent process can paint a child process's layer tree while
 *    the child process is blocked, say on content script.  This is
 *    important on mobile devices where UI responsiveness is key.
 *
 *  - a dedicated "compositor" process can asynchronously (wrt the
 *    browser process) composite and animate layer trees, allowing a
 *    form of pipeline parallelism between compositor/browser/content
 *
 *  - a dedicated "compositor" process can take all responsibility for
 *    accessing the GPU, which is desirable on systems with
 *    buggy/leaky drivers because the compositor process can die while
 *    browser and content live on (and failover mechanisms can be
 *    installed to quickly bring up a replacement compositor)
 *
 * The Layers model has a crisply defined API, which makes it easy to
 * safely "share" layer trees.  The ShadowLayers API extends Layers to
 * allow a remote, parent process to access a child process's layer
 * tree.
 *
 * ShadowLayerForwarder publishes a child context's layer tree to a
 * parent context.  This comprises recording layer-tree modifications
 * into atomic transactions and pushing them over IPC.
 *
 * LayerManagerComposite grafts layer subtrees published by child-context
 * ShadowLayerForwarder(s) into a parent-context layer tree.
 *
 * (Advanced note: because our process tree may have a height >2, a
 * non-leaf subprocess may both receive updates from child processes
 * and publish them to parent processes.  Put another way,
 * LayerManagers may be both LayerManagerComposites and
 * ShadowLayerForwarders.)
 *
 * There are only shadow types for layers that have different shadow
 * vs. not-shadow behavior.  ColorLayers and ContainerLayers behave
 * the same way in both regimes (so far).
 *
 *
 * The mecanism to shadow the layer tree on the compositor through IPC works as
 * follows:
 * The layer tree is managed on the content thread, and shadowed in the compositor
 * thread. The shadow layer tree is only kept in sync with whatever happens in
 * the content thread. To do this we use IPDL protocols. IPDL is a domain
 * specific language that describes how two processes or thread should
 * communicate. C++ code is generated from .ipdl files to implement the message
 * passing, synchronization and serialization logic. To use the generated code
 * we implement classes that inherit the generated IPDL actor. the ipdl actors
 * of a protocol PX are PXChild or PXParent (the generated class), and we
 * conventionally implement XChild and XParent. The Parent side of the protocol
 * is the one that lives on the compositor thread. Think of IPDL actors as
 * endpoints of communication. they are useful to send messages and also to
 * dispatch the message to the right actor on the other side. One nice property
 * of an IPDL actor is that when an actor, say PXChild is sent in a message, the
 * PXParent comes out in the other side. we use this property a lot to dispatch
 * messages to the right layers and compositable, each of which have their own
 * ipdl actor on both side.
 *
 * Most of the synchronization logic happens in layer transactions and
 * compositable transactions.
 * A transaction is a set of changes to the layers and/or the compositables
 * that are sent and applied together to the compositor thread to keep the
 * LayerComposite in a coherent state.
 * Layer transactions maintain the shape of the shadow layer tree, and
 * synchronize the texture data held by compositables. Layer transactions
 * are always between the content thread and the compositor thread.
 * Compositable transactions are subset of a layer transaction with which only
 * compositables and textures can be manipulated, and does not always originate
 * from the content thread. (See CompositableForwarder.h and ImageBridgeChild.h)
 */

class ShadowLayerForwarder final : public CompositableForwarder
                                 , public ShmemAllocator
                                 , public LegacySurfaceDescriptorAllocator
{
  friend class ClientLayerManager;

public:
  virtual ~ShadowLayerForwarder();

  virtual ShmemAllocator* AsShmemAllocator() override { return this; }

  virtual ShadowLayerForwarder* AsLayerForwarder() override { return this; }

  virtual LegacySurfaceDescriptorAllocator*
  AsLegacySurfaceDescriptorAllocator() override { return this; }

  FixedSizeSmallShmemSectionAllocator* GetTileLockAllocator();

  /**
   * Setup the IPDL actor for aCompositable to be part of layers
   * transactions.
   */
  virtual void Connect(CompositableClient* aCompositable,
                       ImageContainer* aImageContainer) override;

  virtual PTextureChild* CreateTexture(const SurfaceDescriptor& aSharedData,
                                       LayersBackend aLayersBackend,
                                       TextureFlags aFlags) override;

  /**
   * Adds an edit in the layers transaction in order to attach
   * the corresponding compositable and layer on the compositor side.
   * Connect must have been called on aCompositable beforehand.
   */
  void Attach(CompositableClient* aCompositable,
              ShadowableLayer* aLayer);

  /**
   * Adds an edit in the transaction in order to attach a Compositable that
   * is not managed by this ShadowLayerForwarder (for example, by ImageBridge
   * in the case of async-video).
   * Since the compositable is not managed by this forwarder, we can't use
   * the compositable or it's IPDL actor here, so we use an ID instead, that
   * is matched on the compositor side.
   */
  void AttachAsyncCompositable(uint64_t aCompositableID,
                               ShadowableLayer* aLayer);

  /**
   * Begin recording a transaction to be forwarded atomically to a
   * LayerManagerComposite.
   */
  void BeginTransaction(const gfx::IntRect& aTargetBounds,
                        ScreenRotation aRotation,
                        mozilla::dom::ScreenOrientationInternal aOrientation);

  /**
   * The following methods may only be called after BeginTransaction()
   * but before EndTransaction().  They mirror the LayerManager
   * interface in Layers.h.
   */

  /**
   * Notify the shadow manager that a new, "real" layer has been
   * created, and a corresponding shadow layer should be created in
   * the compositing process.
   */
  void CreatedPaintedLayer(ShadowableLayer* aThebes);
  void CreatedContainerLayer(ShadowableLayer* aContainer);
  void CreatedImageLayer(ShadowableLayer* aImage);
  void CreatedColorLayer(ShadowableLayer* aColor);
  void CreatedCanvasLayer(ShadowableLayer* aCanvas);
  void CreatedRefLayer(ShadowableLayer* aRef);

  /**
   * At least one attribute of |aMutant| has changed, and |aMutant|
   * needs to sync to its shadow layer.  This initial implementation
   * forwards all attributes when any is mutated.
   */
  void Mutated(ShadowableLayer* aMutant);

  void SetRoot(ShadowableLayer* aRoot);
  /**
   * Insert |aChild| after |aAfter| in |aContainer|.  |aAfter| can be
   * nullptr to indicated that |aChild| should be appended to the end of
   * |aContainer|'s child list.
   */
  void InsertAfter(ShadowableLayer* aContainer,
                   ShadowableLayer* aChild,
                   ShadowableLayer* aAfter = nullptr);
  void RemoveChild(ShadowableLayer* aContainer,
                   ShadowableLayer* aChild);
  void RepositionChild(ShadowableLayer* aContainer,
                       ShadowableLayer* aChild,
                       ShadowableLayer* aAfter = nullptr);

  /**
   * Set aMaskLayer as the mask on aLayer.
   * Note that only image layers are properly supported
   * LayerTransactionParent::UpdateMask and accompanying ipdl
   * will need changing to update properties for other kinds
   * of mask layer.
   */
  void SetMask(ShadowableLayer* aLayer,
               ShadowableLayer* aMaskLayer);

  /**
   * See CompositableForwarder::UseTiledLayerBuffer
   */
  virtual void UseTiledLayerBuffer(CompositableClient* aCompositable,
                                   const SurfaceDescriptorTiles& aTileLayerDescriptor) override;

  virtual bool DestroyInTransaction(PTextureChild* aTexture, bool synchronously) override;
  virtual bool DestroyInTransaction(PCompositableChild* aCompositable, bool synchronously) override;

  virtual void RemoveTextureFromCompositable(CompositableClient* aCompositable,
                                             TextureClient* aTexture) override;

  virtual void RemoveTextureFromCompositableAsync(AsyncTransactionTracker* aAsyncTransactionTracker,
                                                  CompositableClient* aCompositable,
                                                  TextureClient* aTexture) override;

  /**
   * Communicate to the compositor that aRegion in the texture identified by aLayer
   * and aIdentifier has been updated to aThebesBuffer.
   */
  virtual void UpdateTextureRegion(CompositableClient* aCompositable,
                                   const ThebesBufferData& aThebesBufferData,
                                   const nsIntRegion& aUpdatedRegion) override;

  /**
   * See CompositableForwarder::UseTextures
   */
  virtual void UseTextures(CompositableClient* aCompositable,
                           const nsTArray<TimedTextureClient>& aTextures) override;
  virtual void UseComponentAlphaTextures(CompositableClient* aCompositable,
                                         TextureClient* aClientOnBlack,
                                         TextureClient* aClientOnWhite) override;
#ifdef MOZ_WIDGET_GONK
  virtual void UseOverlaySource(CompositableClient* aCompositable,
                                const OverlaySource& aOverlay,
                                const nsIntRect& aPictureRect) override;
#endif

  /**
   * End the current transaction and forward it to LayerManagerComposite.
   * |aReplies| are directions from the LayerManagerComposite to the
   * caller of EndTransaction().
   */
  bool EndTransaction(InfallibleTArray<EditReply>* aReplies,
                      const nsIntRegion& aRegionToClear,
                      uint64_t aId,
                      bool aScheduleComposite,
                      uint32_t aPaintSequenceNumber,
                      bool aIsRepeatTransaction,
                      const mozilla::TimeStamp& aTransactionStart,
                      bool* aSent);

  /**
   * Set an actor through which layer updates will be pushed.
   */
  void SetShadowManager(PLayerTransactionChild* aShadowManager);

  /**
   * Layout calls here to cache current plugin widget configuration
   * data. We ship this across with the rest of the layer updates when
   * we update. Chrome handles applying these changes.
   */
  void StorePluginWidgetConfigurations(const nsTArray<nsIWidget::Configuration>&
                                       aConfigurations);

  void StopReceiveAsyncParentMessge();

  void ClearCachedResources();

  void Composite();

  virtual void SendPendingAsyncMessges() override;

  /**
   * True if this is forwarding to a LayerManagerComposite.
   */
  bool HasShadowManager() const { return !!mShadowManager; }
  LayerTransactionChild* GetShadowManager() const { return mShadowManager.get(); }

  virtual void WindowOverlayChanged() { mWindowOverlayChanged = true; }

  /**
   * The following Alloc/Open/Destroy interfaces abstract over the
   * details of working with surfaces that are shared across
   * processes.  They provide the glue between C++ Layers and the
   * LayerComposite IPC system.
   *
   * The basic lifecycle is
   *
   *  - a Layer needs a buffer.  Its ShadowableLayer subclass calls
   *    AllocBuffer(), then calls one of the Created*Buffer() methods
   *    above to transfer the (temporary) front buffer to its
   *    LayerComposite in the other process.  The Layer needs a
   *    gfxASurface to paint, so the ShadowableLayer uses
   *    OpenDescriptor(backBuffer) to get that surface, and hands it
   *    out to the Layer.
   *
   * - a Layer has painted new pixels.  Its ShadowableLayer calls one
   *   of the Painted*Buffer() methods above with the back buffer
   *   descriptor.  This notification is forwarded to the LayerComposite,
   *   which uses OpenDescriptor() to access the newly-painted pixels.
   *   The LayerComposite then updates its front buffer in a Layer- and
   *   platform-dependent way, and sends a surface descriptor back to
   *   the ShadowableLayer that becomes its new back back buffer.
   *
   * - a Layer wants to destroy its buffers.  Its ShadowableLayer
   *   calls Destroyed*Buffer(), which gives up control of the back
   *   buffer descriptor.  The actual back buffer surface is then
   *   destroyed using DestroySharedSurface() just before notifying
   *   the parent process.  When the parent process is notified, the
   *   LayerComposite also calls DestroySharedSurface() on its front
   *   buffer, and the double-buffer pair is gone.
   */


  virtual bool AllocUnsafeShmem(size_t aSize,
                                mozilla::ipc::SharedMemory::SharedMemoryType aType,
                                mozilla::ipc::Shmem* aShmem) override;
  virtual bool AllocShmem(size_t aSize,
                          mozilla::ipc::SharedMemory::SharedMemoryType aType,
                          mozilla::ipc::Shmem* aShmem) override;
  virtual void DeallocShmem(mozilla::ipc::Shmem& aShmem) override;

  virtual bool IPCOpen() const override;

  virtual bool IsSameProcess() const override;

  virtual MessageLoop* GetMessageLoop() const override { return mMessageLoop; }

  base::ProcessId GetParentPid() const;

  /**
   * Construct a shadow of |aLayer| on the "other side", at the
   * LayerManagerComposite.
   */
  PLayerChild* ConstructShadowFor(ShadowableLayer* aLayer);

  /**
   * Flag the next paint as the first for a document.
   */
  void SetIsFirstPaint() { mIsFirstPaint = true; }

  void SetPaintSyncId(int32_t aSyncId) { mPaintSyncId = aSyncId; }

  static void PlatformSyncBeforeUpdate();

  virtual bool AllocSurfaceDescriptor(const gfx::IntSize& aSize,
                                      gfxContentType aContent,
                                      SurfaceDescriptor* aBuffer) override;

  virtual bool AllocSurfaceDescriptorWithCaps(const gfx::IntSize& aSize,
                                              gfxContentType aContent,
                                              uint32_t aCaps,
                                              SurfaceDescriptor* aBuffer) override;

  virtual void DestroySurfaceDescriptor(SurfaceDescriptor* aSurface) override;

  // Returns true if aSurface wraps a Shmem.
  static bool IsShmem(SurfaceDescriptor* aSurface);

protected:
  ShadowLayerForwarder();

#ifdef DEBUG
  void CheckSurfaceDescriptor(const SurfaceDescriptor* aDescriptor) const;
#else
  void CheckSurfaceDescriptor(const SurfaceDescriptor* aDescriptor) const {}
#endif

  bool InWorkerThread();

  RefPtr<LayerTransactionChild> mShadowManager;

private:

  Transaction* mTxn;
  MessageLoop* mMessageLoop;
  std::vector<AsyncChildMessageData> mPendingAsyncMessages;
  DiagnosticTypes mDiagnosticTypes;
  bool mIsFirstPaint;
  bool mWindowOverlayChanged;
  int32_t mPaintSyncId;
  InfallibleTArray<PluginWindowData> mPluginWindowData;
  FixedSizeSmallShmemSectionAllocator* mSectionAllocator;
};

class CompositableClient;

/**
 * A ShadowableLayer is a Layer can be shared with a parent context
 * through a ShadowLayerForwarder.  A ShadowableLayer maps to a
 * Shadow*Layer in a parent context.
 *
 * Note that ShadowLayers can themselves be ShadowableLayers.
 */
class ShadowableLayer
{
public:
  virtual ~ShadowableLayer() {}

  virtual Layer* AsLayer() = 0;

  /**
   * True if this layer has a shadow in a parent process.
   */
  bool HasShadow() { return !!mShadow; }

  /**
   * Return the IPC handle to a Shadow*Layer referring to this if one
   * exists, nullptr if not.
   */
  PLayerChild* GetShadow() { return mShadow; }

  virtual CompositableClient* GetCompositableClient() { return nullptr; }
protected:
  ShadowableLayer() : mShadow(nullptr) {}

  PLayerChild* mShadow;
};

/// A simple shmem section allocator that can only allocate small
/// fixed size elements (only intended to be used to store tile
/// copy-on-write locks for now).
class FixedSizeSmallShmemSectionAllocator final : public ShmemSectionAllocator
{
public:
  enum AllocationStatus
  {
    STATUS_ALLOCATED,
    STATUS_FREED
  };

  struct ShmemSectionHeapHeader
  {
    Atomic<uint32_t> mTotalBlocks;
    Atomic<uint32_t> mAllocatedBlocks;
  };

  struct ShmemSectionHeapAllocation
  {
    Atomic<uint32_t> mStatus;
    uint32_t mSize;
  };

  explicit FixedSizeSmallShmemSectionAllocator(ShmemAllocator* aShmProvider);

  ~FixedSizeSmallShmemSectionAllocator();

  virtual bool AllocShmemSection(uint32_t aSize, ShmemSection* aShmemSection) override;

  virtual void DeallocShmemSection(ShmemSection& aShmemSection) override;

  virtual void MemoryPressure() override { ShrinkShmemSectionHeap(); }

  // can be called on the compositor process.
  static void FreeShmemSection(ShmemSection& aShmemSection);

  void ShrinkShmemSectionHeap();

protected:
  std::vector<mozilla::ipc::Shmem> mUsedShmems;
  ShmemAllocator* mShmProvider;
};

} // namespace layers
} // namespace mozilla

#endif // ifndef mozilla_layers_ShadowLayers_h
