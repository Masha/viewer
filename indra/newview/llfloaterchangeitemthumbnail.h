/** 
 * @file llfloaterchangeitemthumbnail.h
 * @brief LLFloaterChangeItemThumbnail class definition
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLFLOATERCHANGEITEMTHUMBNAIL_H
#define LL_LLFLOATERCHANGEITEMTHUMBNAIL_H

#include "llfloater.h"
#include "llinventoryobserver.h"
#include "llvoinventorylistener.h"

class LLButton;
class LLIconCtrl;
class LLTextBox;
class LLThumbnailCtrl;
class LLUICtrl;
class LLViewerInventoryItem;

class LLFloaterChangeItemThumbnail : public LLFloater, public LLInventoryObserver, public LLVOInventoryListener
{
public:
    LLFloaterChangeItemThumbnail(const LLSD& key);
	~LLFloaterChangeItemThumbnail();

    BOOL postBuild() override;
    void onOpen(const LLSD& key) override;

    void changed(U32 mask) override;
    void inventoryChanged(LLViewerObject* object,
        LLInventoryObject::object_list_t* inventory,
        S32 serial_num,
        void* user_data) override;

private:

    void refreshFromInventory();
    void refreshFromItem(LLViewerInventoryItem* item);

    void startObjectInventoryObserver();
    void stopObjectInventoryObserver();

    static void onUploadLocal(void*);
    static void onUploadSnapshot(void*);
    static void onUseTexture(void*);
    static void onCopyToClipboard(void*);
    static void onPasteFromClipboard(void*);
    static void onRemove(void*);

    enum EToolTipState
    {
        TOOLTIP_NONE,
        TOOLTIP_UPLOAD_LOCAL,
        TOOLTIP_UPLOAD_SNAPSHOT,
        TOOLTIP_USE_TEXTURE,
        TOOLTIP_COPY_TO_CLIPBOARD,
        TOOLTIP_COPY_FROM_CLIPBOARD,
        TOOLTIP_REMOVE,
    };

    void onButtonMouseEnter(LLUICtrl* button, const LLSD& param, EToolTipState state);
    void onButtonMouseLeave(LLUICtrl* button, const LLSD& param, EToolTipState state);

    bool mObserverInitialized;
    EToolTipState mTooltipState;
    LLUUID mItemId;
    LLUUID mTaskId;

    LLIconCtrl *mItemTypeIcon;
    LLUICtrl *mItemNameText;
    LLThumbnailCtrl *mThumbnailCtrl;
    LLTextBox *mToolTipTextBox;
    LLButton *mCopyToClipboardBtn;
    LLButton *mPasteFromClipboardBtn;
    LLButton *mRemoveImageBtn;
};
#endif  // LL_LLFLOATERCHANGEITEMTHUMBNAIL_H
