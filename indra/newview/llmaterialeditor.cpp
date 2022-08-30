/** 
 * @file llmaterialeditor.cpp
 * @brief Implementation of the notecard editor
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#include "llviewerprecompiledheaders.h"

#include "llmaterialeditor.h"

#include "llagent.h"
#include "llagentbenefits.h"
#include "llappviewer.h"
#include "llcombobox.h"
#include "llinventorymodel.h"
#include "llnotificationsutil.h"
#include "lltexturectrl.h"
#include "lltrans.h"
#include "llviewermenufile.h"
#include "llviewertexture.h"
#include "llsdutil.h"
#include "llselectmgr.h"
#include "llstatusbar.h"	// can_afford_transaction()
#include "llviewerinventory.h"
#include "llinventory.h"
#include "llviewerregion.h"
#include "llvovolume.h"
#include "roles_constants.h"
#include "llviewerobjectlist.h"
#include "llfloaterreg.h"
#include "llfilesystem.h"
#include "llsdserialize.h"
#include "llimagej2c.h"
#include "llviewertexturelist.h"
#include "llfloaterperms.h"

#include "tinygltf/tiny_gltf.h"
#include "lltinygltfhelper.h"
#include <strstream>


const std::string MATERIAL_ALBEDO_DEFAULT_NAME = "Albedo";
const std::string MATERIAL_NORMAL_DEFAULT_NAME = "Normal";
const std::string MATERIAL_METALLIC_DEFAULT_NAME = "Metallic Roughness";
const std::string MATERIAL_EMISSIVE_DEFAULT_NAME = "Emissive";


class LLMaterialEditorCopiedCallback : public LLInventoryCallback
{
public:
    LLMaterialEditorCopiedCallback(const std::string &buffer, const LLUUID &old_item_id) : mBuffer(buffer), mOldItemId(old_item_id) {}

    virtual void fire(const LLUUID& inv_item_id)
    {
        LLMaterialEditor::finishSaveAs(mOldItemId, inv_item_id, mBuffer);
    }

private:
    std::string mBuffer;
    LLUUID mOldItemId;
};

///----------------------------------------------------------------------------
/// Class LLMaterialEditor
///----------------------------------------------------------------------------

// Default constructor
LLMaterialEditor::LLMaterialEditor(const LLSD& key)
    : LLPreview(key)
    , mHasUnsavedChanges(false)
    , mExpectedUploadCost(0)
    , mUploadingTexturesCount(0)
{
    const LLInventoryItem* item = getItem();
    if (item)
    {
        mAssetID = item->getAssetUUID();
    }
}

BOOL LLMaterialEditor::postBuild()
{
    mAlbedoTextureCtrl = getChild<LLTextureCtrl>("albedo_texture");
    mMetallicTextureCtrl = getChild<LLTextureCtrl>("metallic_roughness_texture");
    mEmissiveTextureCtrl = getChild<LLTextureCtrl>("emissive_texture");
    mNormalTextureCtrl = getChild<LLTextureCtrl>("normal_texture");

    mAlbedoTextureCtrl->setCommitCallback(boost::bind(&LLMaterialEditor::onCommitAlbedoTexture, this, _1, _2));
    mMetallicTextureCtrl->setCommitCallback(boost::bind(&LLMaterialEditor::onCommitMetallicTexture, this, _1, _2));
    mEmissiveTextureCtrl->setCommitCallback(boost::bind(&LLMaterialEditor::onCommitEmissiveTexture, this, _1, _2));
    mNormalTextureCtrl->setCommitCallback(boost::bind(&LLMaterialEditor::onCommitNormalTexture, this, _1, _2));

    childSetAction("save", boost::bind(&LLMaterialEditor::onClickSave, this));
    childSetAction("save_as", boost::bind(&LLMaterialEditor::onClickSaveAs, this));
    childSetAction("cancel", boost::bind(&LLMaterialEditor::onClickCancel, this));

    S32 upload_cost = LLAgentBenefitsMgr::current().getTextureUploadCost();
    getChild<LLUICtrl>("albedo_upload_fee")->setTextArg("[FEE]", llformat("%d", upload_cost));
    getChild<LLUICtrl>("metallic_upload_fee")->setTextArg("[FEE]", llformat("%d", upload_cost));
    getChild<LLUICtrl>("emissive_upload_fee")->setTextArg("[FEE]", llformat("%d", upload_cost));
    getChild<LLUICtrl>("normal_upload_fee")->setTextArg("[FEE]", llformat("%d", upload_cost));

    boost::function<void(LLUICtrl*, void*)> changes_callback = [this](LLUICtrl * ctrl, void*)
    {
        setHasUnsavedChanges(true);
        // Apply changes to object live
        applyToSelection();
    };
 
    childSetCommitCallback("double sided", changes_callback, NULL);

    // Albedo
    childSetCommitCallback("albedo color", changes_callback, NULL);
    childSetCommitCallback("transparency", changes_callback, NULL);
    childSetCommitCallback("alpha mode", changes_callback, NULL);
    childSetCommitCallback("alpha cutoff", changes_callback, NULL);

    // Metallic-Roughness
    childSetCommitCallback("metalness factor", changes_callback, NULL);
    childSetCommitCallback("roughness factor", changes_callback, NULL);

    // Metallic-Roughness
    childSetCommitCallback("metalness factor", changes_callback, NULL);
    childSetCommitCallback("roughness factor", changes_callback, NULL);

    // Emissive
    childSetCommitCallback("emissive color", changes_callback, NULL);

    childSetVisible("unsaved_changes", mHasUnsavedChanges);

    // Todo:
    // Disable/enable setCanApplyImmediately() based on
    // working from inventory, upload or editing inworld

	return LLPreview::postBuild();
}

void LLMaterialEditor::onClickCloseBtn(bool app_quitting)
{
    if (app_quitting)
    {
        closeFloater(app_quitting);
    }
    else
    {
        onClickCancel();
    }
}

void LLMaterialEditor::onClose(bool app_quitting)
{
    // todo: will only revert whatever was recently selected,
    // Later should work based of tools floater
    LLSelectMgr::getInstance()->selectionRevertGLTFMaterials();
    
    LLPreview::onClose(app_quitting);
}

LLUUID LLMaterialEditor::getAlbedoId()
{
    return mAlbedoTextureCtrl->getValue().asUUID();
}

void LLMaterialEditor::setAlbedoId(const LLUUID& id)
{
    mAlbedoTextureCtrl->setValue(id);
    mAlbedoTextureCtrl->setDefaultImageAssetID(id);
}

void LLMaterialEditor::setAlbedoUploadId(const LLUUID& id)
{
    // Might be better to use local textures and
    // assign a fee in case of a local texture
    if (id.notNull())
    {
        // todo: this does not account for posibility of texture
        // being from inventory, need to check that
        childSetValue("albedo_upload_fee", getString("upload_fee_string"));
        // Only set if we will need to upload this texture
        mAlbedoTextureUploadId = id;
    }
    setHasUnsavedChanges(true);
}

LLColor4 LLMaterialEditor::getAlbedoColor()
{
    LLColor4 ret = LLColor4(childGetValue("albedo color"));
    ret.mV[3] = getTransparency();
    return ret;
}

void LLMaterialEditor::setAlbedoColor(const LLColor4& color)
{
    childSetValue("albedo color", color.getValue());
    setTransparency(color.mV[3]);
}

F32 LLMaterialEditor::getTransparency()
{
    return childGetValue("transparency").asReal();
}

void LLMaterialEditor::setTransparency(F32 transparency)
{
    childSetValue("transparency", transparency);
}

std::string LLMaterialEditor::getAlphaMode()
{
    return childGetValue("alpha mode").asString();
}

void LLMaterialEditor::setAlphaMode(const std::string& alpha_mode)
{
    childSetValue("alpha mode", alpha_mode);
}

F32 LLMaterialEditor::getAlphaCutoff()
{
    return childGetValue("alpha cutoff").asReal();
}

void LLMaterialEditor::setAlphaCutoff(F32 alpha_cutoff)
{
    childSetValue("alpha cutoff", alpha_cutoff);
}

void LLMaterialEditor::setMaterialName(const std::string &name)
{
    setTitle(name);
    mMaterialName = name;
}

LLUUID LLMaterialEditor::getMetallicRoughnessId()
{
    return mMetallicTextureCtrl->getValue().asUUID();
}

void LLMaterialEditor::setMetallicRoughnessId(const LLUUID& id)
{
    mMetallicTextureCtrl->setValue(id);
    mMetallicTextureCtrl->setDefaultImageAssetID(id);
}

void LLMaterialEditor::setMetallicRoughnessUploadId(const LLUUID& id)
{
    if (id.notNull())
    {
        // todo: this does not account for posibility of texture
        // being from inventory, need to check that
        childSetValue("metallic_upload_fee", getString("upload_fee_string"));
        mMetallicTextureUploadId = id;
    }
    setHasUnsavedChanges(true);
}

F32 LLMaterialEditor::getMetalnessFactor()
{
    return childGetValue("metalness factor").asReal();
}

void LLMaterialEditor::setMetalnessFactor(F32 factor)
{
    childSetValue("metalness factor", factor);
}

F32 LLMaterialEditor::getRoughnessFactor()
{
    return childGetValue("roughness factor").asReal();
}

void LLMaterialEditor::setRoughnessFactor(F32 factor)
{
    childSetValue("roughness factor", factor);
}

LLUUID LLMaterialEditor::getEmissiveId()
{
    return mEmissiveTextureCtrl->getValue().asUUID();
}

void LLMaterialEditor::setEmissiveId(const LLUUID& id)
{
    mEmissiveTextureCtrl->setValue(id);
    mEmissiveTextureCtrl->setDefaultImageAssetID(id);
}

void LLMaterialEditor::setEmissiveUploadId(const LLUUID& id)
{
    if (id.notNull())
    {
        // todo: this does not account for posibility of texture
        // being from inventory, need to check that
        childSetValue("emissive_upload_fee", getString("upload_fee_string"));
        mEmissiveTextureUploadId = id;
    }
    setHasUnsavedChanges(true);
}

LLColor4 LLMaterialEditor::getEmissiveColor()
{
    return LLColor4(childGetValue("emissive color"));
}

void LLMaterialEditor::setEmissiveColor(const LLColor4& color)
{
    childSetValue("emissive color", color.getValue());
}

LLUUID LLMaterialEditor::getNormalId()
{
    return mNormalTextureCtrl->getValue().asUUID();
}

void LLMaterialEditor::setNormalId(const LLUUID& id)
{
    mNormalTextureCtrl->setValue(id);
    mNormalTextureCtrl->setDefaultImageAssetID(id);
}

void LLMaterialEditor::setNormalUploadId(const LLUUID& id)
{
    if (id.notNull())
    {
        // todo: this does not account for posibility of texture
        // being from inventory, need to check that
        childSetValue("normal_upload_fee", getString("upload_fee_string"));
        mNormalTextureUploadId = id;
    }
    setHasUnsavedChanges(true);
}

bool LLMaterialEditor::getDoubleSided()
{
    return childGetValue("double sided").asBoolean();
}

void LLMaterialEditor::setDoubleSided(bool double_sided)
{
    childSetValue("double sided", double_sided);
}

void LLMaterialEditor::setHasUnsavedChanges(bool value)
{
    if (value != mHasUnsavedChanges)
    {
        mHasUnsavedChanges = value;
        childSetVisible("unsaved_changes", value);
    }

    S32 upload_texture_count = 0;
    if (mAlbedoTextureUploadId.notNull() && mAlbedoTextureUploadId == getAlbedoId())
    {
        upload_texture_count++;
    }
    if (mMetallicTextureUploadId.notNull() && mMetallicTextureUploadId == getMetallicRoughnessId())
    {
        upload_texture_count++;
    }
    if (mEmissiveTextureUploadId.notNull() && mEmissiveTextureUploadId == getEmissiveId())
    {
        upload_texture_count++;
    }
    if (mNormalTextureUploadId.notNull() && mNormalTextureUploadId == getNormalId())
    {
        upload_texture_count++;
    }

    mExpectedUploadCost = upload_texture_count * LLAgentBenefitsMgr::current().getTextureUploadCost();
    getChild<LLUICtrl>("total_upload_fee")->setTextArg("[FEE]", llformat("%d", mExpectedUploadCost));
}

void LLMaterialEditor::setCanSaveAs(BOOL value)
{
    childSetEnabled("save_as", value);
}

void LLMaterialEditor::setCanSave(BOOL value)
{
    childSetEnabled("save", value);
}

void LLMaterialEditor::onCommitAlbedoTexture(LLUICtrl * ctrl, const LLSD & data)
{
    // might be better to use arrays, to have a single callback
    // and not to repeat the same thing for each tecture control
    LLUUID new_val = mAlbedoTextureCtrl->getValue().asUUID();
    if (new_val == mAlbedoTextureUploadId && mAlbedoTextureUploadId.notNull())
    {
        childSetValue("albedo_upload_fee", getString("upload_fee_string"));
    }
    else
    {
        // Texture picker has 'apply now' with 'cancel' support.
        // Keep mAlbedoJ2C and mAlbedoFetched, it's our storage in
        // case user decides to cancel changes.
        // Without mAlbedoFetched, viewer will eventually cleanup
        // the texture that is not in use
        childSetValue("albedo_upload_fee", getString("no_upload_fee_string"));
    }
    setHasUnsavedChanges(true);
    applyToSelection();
}

void LLMaterialEditor::onCommitMetallicTexture(LLUICtrl * ctrl, const LLSD & data)
{
    LLUUID new_val = mMetallicTextureCtrl->getValue().asUUID();
    if (new_val == mMetallicTextureUploadId && mMetallicTextureUploadId.notNull())
    {
        childSetValue("metallic_upload_fee", getString("upload_fee_string"));
    }
    else
    {
        childSetValue("metallic_upload_fee", getString("no_upload_fee_string"));
    }
    setHasUnsavedChanges(true);
    applyToSelection();
}

void LLMaterialEditor::onCommitEmissiveTexture(LLUICtrl * ctrl, const LLSD & data)
{
    LLUUID new_val = mEmissiveTextureCtrl->getValue().asUUID();
    if (new_val == mEmissiveTextureUploadId && mEmissiveTextureUploadId.notNull())
    {
        childSetValue("emissive_upload_fee", getString("upload_fee_string"));
    }
    else
    {
        childSetValue("emissive_upload_fee", getString("no_upload_fee_string"));
    }
    setHasUnsavedChanges(true);
    applyToSelection();
}

void LLMaterialEditor::onCommitNormalTexture(LLUICtrl * ctrl, const LLSD & data)
{
    LLUUID new_val = mNormalTextureCtrl->getValue().asUUID();
    if (new_val == mNormalTextureUploadId && mNormalTextureUploadId.notNull())
    {
        childSetValue("normal_upload_fee", getString("upload_fee_string"));
    }
    else
    {
        childSetValue("normal_upload_fee", getString("no_upload_fee_string"));
    }
    setHasUnsavedChanges(true);
    applyToSelection();
}


static void write_color(const LLColor4& color, std::vector<double>& c)
{
    for (int i = 0; i < c.size(); ++i) // NOTE -- use c.size because some gltf colors are 3-component
    {
        c[i] = color.mV[i];
    }
}

static U32 write_texture(const LLUUID& id, tinygltf::Model& model)
{
    tinygltf::Image image;
    image.uri = id.asString();
    model.images.push_back(image);
    U32 image_idx = model.images.size() - 1;

    tinygltf::Texture texture;
    texture.source = image_idx;
    model.textures.push_back(texture);
    U32 texture_idx = model.textures.size() - 1;

    return texture_idx;
}


void LLMaterialEditor::onClickSave()
{
    if (!can_afford_transaction(mExpectedUploadCost))
    {
        LLSD args;
        args["COST"] = llformat("%d", mExpectedUploadCost);
        LLNotificationsUtil::add("ErrorCannotAffordUpload", args);
        return;
    }

    applyToSelection();
    saveIfNeeded();
}


std::string LLMaterialEditor::getGLTFJson(bool prettyprint)
{
    tinygltf::Model model;
    getGLTFModel(model);

    std::ostringstream str;

    tinygltf::TinyGLTF gltf;
    
    gltf.WriteGltfSceneToStream(&model, str, prettyprint, false);

    std::string dump = str.str();

    return dump;
}

void LLMaterialEditor::getGLBData(std::vector<U8>& data)
{
    tinygltf::Model model;
    getGLTFModel(model);

    std::ostringstream str;

    tinygltf::TinyGLTF gltf;

    gltf.WriteGltfSceneToStream(&model, str, false, true);

    std::string dump = str.str();

    data.resize(dump.length());

    memcpy(&data[0], dump.c_str(), dump.length());
}

void LLMaterialEditor::getGLTFModel(tinygltf::Model& model)
{
    model.materials.resize(1);
    tinygltf::PbrMetallicRoughness& pbrMaterial = model.materials[0].pbrMetallicRoughness;

    // write albedo
    LLColor4 albedo_color = getAlbedoColor();
    albedo_color.mV[3] = getTransparency();
    write_color(albedo_color, pbrMaterial.baseColorFactor);

    model.materials[0].alphaCutoff = getAlphaCutoff();
    model.materials[0].alphaMode = getAlphaMode();

    LLUUID albedo_id = getAlbedoId();

    if (albedo_id.notNull())
    {
        U32 texture_idx = write_texture(albedo_id, model);

        pbrMaterial.baseColorTexture.index = texture_idx;
    }

    // write metallic/roughness
    F32 metalness = getMetalnessFactor();
    F32 roughness = getRoughnessFactor();

    pbrMaterial.metallicFactor = metalness;
    pbrMaterial.roughnessFactor = roughness;

    LLUUID mr_id = getMetallicRoughnessId();
    if (mr_id.notNull())
    {
        U32 texture_idx = write_texture(mr_id, model);
        pbrMaterial.metallicRoughnessTexture.index = texture_idx;
    }

    //write emissive
    LLColor4 emissive_color = getEmissiveColor();
    model.materials[0].emissiveFactor.resize(3);
    write_color(emissive_color, model.materials[0].emissiveFactor);

    LLUUID emissive_id = getEmissiveId();
    if (emissive_id.notNull())
    {
        U32 idx = write_texture(emissive_id, model);
        model.materials[0].emissiveTexture.index = idx;
    }

    //write normal
    LLUUID normal_id = getNormalId();
    if (normal_id.notNull())
    {
        U32 idx = write_texture(normal_id, model);
        model.materials[0].normalTexture.index = idx;
    }

    //write doublesided
    model.materials[0].doubleSided = getDoubleSided();

    model.asset.version = "2.0";
}

std::string LLMaterialEditor::getEncodedAsset()
{
    LLSD asset;
    asset["version"] = "1.0";
    asset["type"] = "GLTF 2.0";
    asset["data"] = getGLTFJson(false);

    std::ostringstream str;
    LLSDSerialize::serialize(asset, str, LLSDSerialize::LLSD_BINARY);

    return str.str();
}

bool LLMaterialEditor::decodeAsset(const std::vector<char>& buffer)
{
    LLSD asset;
    
    std::istrstream str(&buffer[0], buffer.size());
    if (LLSDSerialize::deserialize(asset, str, buffer.size()))
    {
        if (asset.has("version") && asset["version"] == "1.0")
        {
            if (asset.has("type") && asset["type"] == "GLTF 2.0")
            {
                if (asset.has("data") && asset["data"].isString())
                {
                    std::string data = asset["data"];

                    tinygltf::TinyGLTF gltf;
                    tinygltf::TinyGLTF loader;
                    std::string        error_msg;
                    std::string        warn_msg;

                    tinygltf::Model model_in;

                    if (loader.LoadASCIIFromString(&model_in, &error_msg, &warn_msg, data.c_str(), data.length(), ""))
                    {
                        return setFromGltfModel(model_in, true);
                    }
                    else
                    {
                        LL_WARNS() << "Failed to decode material asset: " << LL_ENDL;
                        LL_WARNS() << warn_msg << LL_ENDL;
                        LL_WARNS() << error_msg << LL_ENDL;
                    }
                }
            }
        }
    }
    else
    {
        LL_WARNS() << "Failed to deserialize material LLSD" << LL_ENDL;
    }

    return false;
}

/**
 * Build a description of the material we just imported.
 * Currently this means a list of the textures present but we
 * may eventually want to make it more complete - will be guided
 * by what the content creators say they need.
 */
const std::string LLMaterialEditor::buildMaterialDescription()
{
    std::ostringstream desc;
    desc << LLTrans::getString("Material Texture Name Header");

    // add the texture names for each just so long as the material
    // we loaded has an entry for it (i think testing the texture 
    // control UUI for NULL is a valid metric for if it was loaded
    // or not but I suspect this code will change a lot so may need
    // to revisit
    if (!mAlbedoTextureCtrl->getValue().asUUID().isNull())
    {
        desc << mAlbedoName;
        desc << ", ";
    }
    if (!mMetallicTextureCtrl->getValue().asUUID().isNull())
    {
        desc << mMetallicRoughnessName;
        desc << ", ";
    }
    if (!mEmissiveTextureCtrl->getValue().asUUID().isNull())
    {
        desc << mEmissiveName;
        desc << ", ";
    }
    if (!mNormalTextureCtrl->getValue().asUUID().isNull())
    {
        desc << mNormalName;
    }

    // trim last char if it's a ',' in case there is no normal texture
    // present and the code above inserts one
    // (no need to check for string length - always has initial string)
    std::string::iterator iter = desc.str().end() - 1;
    if (*iter == ',')
    {
        desc.str().erase(iter);
    }

    // sanitize the material description so that it's compatible with the inventory
    // note: split this up because clang doesn't like operating directly on the
    // str() - error: lvalue reference to type 'basic_string<...>' cannot bind to a
    // temporary of type 'basic_string<...>'
    std::string inv_desc = desc.str();
    LLInventoryObject::correctInventoryName(inv_desc);

    return inv_desc;
}

bool LLMaterialEditor::saveIfNeeded()
{
    if (mUploadingTexturesCount > 0)
    {
        // upload already in progress
        // wait until textures upload
        // will retry saving on callback
        return true;
    }

    if (saveTextures() > 0)
    {
        // started texture upload
        setEnabled(false);
        return true;
    }

    std::string buffer = getEncodedAsset();

    const LLInventoryItem* item = getItem();
    // save it out to database
    if (item)
    {
        if (!saveToInventoryItem(buffer, mItemUUID, mObjectUUID))
        {
            return false;
        }

        if (mCloseAfterSave)
        {
            closeFloater();
        }
        else
        {
            mAssetStatus = PREVIEW_ASSET_LOADING;
            setEnabled(false);
        }
    }
    else
    { //make a new inventory item
#if 1
        // gen a new uuid for this asset
        LLTransactionID tid;
        tid.generate();     // timestamp-based randomization + uniquification
        LLAssetID new_asset_id = tid.makeAssetID(gAgent.getSecureSessionID());
        std::string res_desc = buildMaterialDescription();
        U32 next_owner_perm = LLPermissions::DEFAULT.getMaskNextOwner();
        LLUUID parent = gInventory.findCategoryUUIDForType(LLFolderType::FT_MATERIAL);
        const U8 subtype = NO_INV_SUBTYPE;  // TODO maybe use AT_SETTINGS and LLSettingsType::ST_MATERIAL ?

        create_inventory_item(gAgent.getID(), gAgent.getSessionID(), parent, tid, mMaterialName, res_desc,
            LLAssetType::AT_MATERIAL, LLInventoryType::IT_MATERIAL, subtype, next_owner_perm,
            new LLBoostFuncInventoryCallback([output = buffer](LLUUID const& inv_item_id) {
                // from reference in LLSettingsVOBase::createInventoryItem()/updateInventoryItem()
                LLResourceUploadInfo::ptr_t uploadInfo =
                    std::make_shared<LLBufferedAssetUploadInfo>(
                        inv_item_id,
                        LLAssetType::AT_MATERIAL,
                        output,
                        [](LLUUID item_id, LLUUID new_asset_id, LLUUID new_item_id, LLSD response) {
                            LL_INFOS("Material") << "inventory item uploaded.  item: " << item_id << " asset: " << new_asset_id << " new_item_id: " << new_item_id << " response: " << response << LL_ENDL;
                            LLSD params = llsd::map("ASSET_ID", new_asset_id);
                            LLNotificationsUtil::add("MaterialCreated", params);
                        });

                // todo: apply permissions from textures here if server doesn't
                // if any texture is 'no transfer', material should be 'no transfer' as well
                const LLViewerRegion* region = gAgent.getRegion();
                if (region)
                {
                    std::string agent_url(region->getCapability("UpdateMaterialAgentInventory"));
                    if (agent_url.empty())
                    {
                        LL_ERRS() << "missing required agent inventory cap url" << LL_ENDL;
                    }
                    LLViewerAssetUpload::EnqueueInventoryUpload(agent_url, uploadInfo);
                }
                })
        );

        // We do not update floater with uploaded asset yet, so just close it.
        closeFloater();
#endif
    }
    
    return true;
}

// static
bool LLMaterialEditor::saveToInventoryItem(const std::string &buffer, const LLUUID &item_id, const LLUUID &task_id)
{
    const LLViewerRegion* region = gAgent.getRegion();
    if (!region)
    {
        LL_WARNS() << "Not connected to a region, cannot save material." << LL_ENDL;
        return false;
    }
    std::string agent_url = region->getCapability("UpdateMaterialAgentInventory");
    std::string task_url = region->getCapability("UpdateMaterialTaskInventory");

    if (!agent_url.empty() && !task_url.empty())
    {
        std::string url;
        LLResourceUploadInfo::ptr_t uploadInfo;

        if (task_id.isNull() && !agent_url.empty())
        {
            uploadInfo = std::make_shared<LLBufferedAssetUploadInfo>(item_id, LLAssetType::AT_MATERIAL, buffer,
                [](LLUUID itemId, LLUUID newAssetId, LLUUID newItemId, LLSD) {
                LLMaterialEditor::finishInventoryUpload(itemId, newAssetId, newItemId);
            });
            url = agent_url;
        }
        else if (!task_id.isNull() && !task_url.empty())
        {
            LLUUID object_uuid(task_id);
            uploadInfo = std::make_shared<LLBufferedAssetUploadInfo>(task_id, item_id, LLAssetType::AT_MATERIAL, buffer,
                [object_uuid](LLUUID itemId, LLUUID, LLUUID newAssetId, LLSD) {
                LLMaterialEditor::finishTaskUpload(itemId, newAssetId, object_uuid);
            });
            url = task_url;
        }

        if (!url.empty() && uploadInfo)
        {
            LLViewerAssetUpload::EnqueueInventoryUpload(url, uploadInfo);
        }
        else
        {
            return false;
        }

    }
    else // !gAssetStorage
    {
        LL_WARNS() << "Not connected to an materials capable region." << LL_ENDL;
        return false;
    }

    // todo: apply permissions from textures here if server doesn't
    // if any texture is 'no transfer', material should be 'no transfer' as well

    return true;
}

void LLMaterialEditor::finishInventoryUpload(LLUUID itemId, LLUUID newAssetId, LLUUID newItemId)
{
    // Update the UI with the new asset.
    LLMaterialEditor* me = LLFloaterReg::findTypedInstance<LLMaterialEditor>("material_editor", LLSD(itemId));
    if (me)
    {
        if (newItemId.isNull())
        {
            me->setAssetId(newAssetId);
            me->refreshFromInventory();
        }
        else if (newItemId.notNull())
        {
            // Not supposed to happen?
            me->refreshFromInventory(newItemId);
        }
        else
        {
            me->refreshFromInventory(itemId);
        }
    }
}

void LLMaterialEditor::finishTaskUpload(LLUUID itemId, LLUUID newAssetId, LLUUID taskId)
{
    LLMaterialEditor* me = LLFloaterReg::findTypedInstance<LLMaterialEditor>("material_editor", LLSD(itemId));
    if (me)
    {
        me->setAssetId(newAssetId);
        me->refreshFromInventory();
    }
}

void LLMaterialEditor::finishSaveAs(const LLUUID &oldItemId, const LLUUID &newItemId, const std::string &buffer)
{
    LLMaterialEditor* me = LLFloaterReg::findTypedInstance<LLMaterialEditor>("material_editor", LLSD(oldItemId));
    LLViewerInventoryItem* item = gInventory.getItem(newItemId);
    if (item)
    {
        if (me)
        {
            me->mItemUUID = newItemId;
            me->setKey(LLSD(newItemId)); // for findTypedInstance
            me->setMaterialName(item->getName());
            if (!saveToInventoryItem(buffer, newItemId, LLUUID::null))
            {
                me->setEnabled(true);
            }
        }
        else
        {
            saveToInventoryItem(buffer, newItemId, LLUUID::null);
        }
    }
    else if (me)
    {
        me->setEnabled(true);
        LL_WARNS() << "Item does not exist" << LL_ENDL;
    }
}

void LLMaterialEditor::refreshFromInventory(const LLUUID& new_item_id)
{
    if (new_item_id.notNull())
    {
        mItemUUID = new_item_id;
        setKey(LLSD(new_item_id));
    }
    LL_DEBUGS() << "LLPreviewNotecard::refreshFromInventory()" << LL_ENDL;
    loadAsset();
}


void LLMaterialEditor::onClickSaveAs()
{
    if (!can_afford_transaction(mExpectedUploadCost))
    {
        LLSD args;
        args["COST"] = llformat("%d", mExpectedUploadCost);
        LLNotificationsUtil::add("ErrorCannotAffordUpload", args);
        return;
    }

    LLSD args;
    args["DESC"] = mMaterialName;

    LLNotificationsUtil::add("SaveMaterialAs", args, LLSD(), boost::bind(&LLMaterialEditor::onSaveAsMsgCallback, this, _1, _2));
}

void LLMaterialEditor::onSaveAsMsgCallback(const LLSD& notification, const LLSD& response)
{
    S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
    if (0 == option)
    {
        std::string new_name = response["message"].asString();
        LLInventoryObject::correctInventoryName(new_name);
        if (!new_name.empty())
        {
            const LLInventoryItem* item = getItem();
            if (item)
            {
                const LLUUID &marketplacelistings_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_MARKETPLACE_LISTINGS, false);
                LLUUID parent_id = item->getParentUUID();
                if (mObjectUUID.notNull() || marketplacelistings_id == parent_id || gInventory.isObjectDescendentOf(item->getUUID(), gInventory.getLibraryRootFolderID()))
                {
                    parent_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_MATERIAL);
                }

                // A two step process, first copy an existing item, then create new asset
                std::string buffer = getEncodedAsset();
                LLPointer<LLInventoryCallback> cb = new LLMaterialEditorCopiedCallback(buffer, item->getUUID());
                copy_inventory_item(
                    gAgent.getID(),
                    item->getPermissions().getOwner(),
                    item->getUUID(),
                    parent_id,
                    new_name,
                    cb);

                mAssetStatus = PREVIEW_ASSET_LOADING;
                setEnabled(false);
            }
            else
            {
                setMaterialName(new_name);
                onClickSave();
            }
        }
        else
        {
            LLNotificationsUtil::add("InvalidMaterialName");
        }
    }
}

void LLMaterialEditor::onClickCancel()
{
    if (mHasUnsavedChanges)
    {
        LLNotificationsUtil::add("UsavedMaterialChanges", LLSD(), LLSD(), boost::bind(&LLMaterialEditor::onCancelMsgCallback, this, _1, _2));
    }
    else
    {
        closeFloater();
    }
}

void LLMaterialEditor::onCancelMsgCallback(const LLSD& notification, const LLSD& response)
{
    S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
    if (0 == option)
    {
        closeFloater();
    }
}

class LLMaterialFilePicker : public LLFilePickerThread
{
public:
    LLMaterialFilePicker();
    virtual void notify(const std::vector<std::string>& filenames);
    static void	textureLoadedCallback(BOOL success, LLViewerFetchedTexture* src_vi, LLImageRaw* src, LLImageRaw* src_aux, S32 discard_level, BOOL final, void* userdata);

};

LLMaterialFilePicker::LLMaterialFilePicker()
    : LLFilePickerThread(LLFilePicker::FFLOAD_MATERIAL)
{
}

void LLMaterialFilePicker::notify(const std::vector<std::string>& filenames)
{
    if (LLAppViewer::instance()->quitRequested())
    {
        return;
    }

    
    if (filenames.size() > 0)
    {
        LLMaterialEditor* me = (LLMaterialEditor*)LLFloaterReg::getInstance("material_editor");
        if (me)
        {
            me->loadMaterialFromFile(filenames[0]);
        }
    }
}

static void pack_textures(
    LLPointer<LLImageRaw>& albedo_img,
    LLPointer<LLImageRaw>& normal_img,
    LLPointer<LLImageRaw>& mr_img,
    LLPointer<LLImageRaw>& emissive_img,
    LLPointer<LLImageRaw>& occlusion_img,
    LLPointer<LLImageJ2C>& albedo_j2c,
    LLPointer<LLImageJ2C>& normal_j2c,
    LLPointer<LLImageJ2C>& mr_j2c,
    LLPointer<LLImageJ2C>& emissive_j2c)
{
    if (albedo_img)
    {
        albedo_j2c = LLViewerTextureList::convertToUploadFile(albedo_img);
    }

    if (normal_img)
    {
        normal_j2c = LLViewerTextureList::convertToUploadFile(normal_img);
    }

    if (mr_img)
    {
        mr_j2c = LLViewerTextureList::convertToUploadFile(mr_img);
    }

    if (emissive_img)
    {
        emissive_j2c = LLViewerTextureList::convertToUploadFile(emissive_img);
    }
}

void LLMaterialFilePicker::textureLoadedCallback(BOOL success, LLViewerFetchedTexture* src_vi, LLImageRaw* src, LLImageRaw* src_aux, S32 discard_level, BOOL final, void* userdata)
{
}


void LLMaterialEditor::loadMaterialFromFile(const std::string& filename)
{
    tinygltf::TinyGLTF loader;
    std::string        error_msg;
    std::string        warn_msg;

    bool loaded = false;
    tinygltf::Model model_in;

    std::string filename_lc = filename;
    LLStringUtil::toLower(filename_lc);

    // Load a tinygltf model fom a file. Assumes that the input filename has already been
    // been sanitized to one of (.gltf , .glb) extensions, so does a simple find to distinguish.
    if (std::string::npos == filename_lc.rfind(".gltf"))
    {  // file is binary
        loaded = loader.LoadBinaryFromFile(&model_in, &error_msg, &warn_msg, filename);
    }
    else
    {  // file is ascii
        loaded = loader.LoadASCIIFromFile(&model_in, &error_msg, &warn_msg, filename);
    }

    if (!loaded)
    {
        LLNotificationsUtil::add("CannotUploadMaterial");
        return;
    }

    if (model_in.materials.empty())
    {
        // materials are missing
        LLNotificationsUtil::add("CannotUploadMaterial");
        return;
    }

    std::string folder = gDirUtilp->getDirName(filename);


    tinygltf::Material material_in = model_in.materials[0];

    tinygltf::Model  model_out;
    model_out.asset.version = "2.0";
    model_out.materials.resize(1);

    // get albedo texture
    LLPointer<LLImageRaw> albedo_img = LLTinyGLTFHelper::getTexture(folder, model_in, material_in.pbrMetallicRoughness.baseColorTexture.index, mAlbedoName);
    // get normal map
    LLPointer<LLImageRaw> normal_img = LLTinyGLTFHelper::getTexture(folder, model_in, material_in.normalTexture.index, mNormalName);
    // get metallic-roughness texture
    LLPointer<LLImageRaw> mr_img = LLTinyGLTFHelper::getTexture(folder, model_in, material_in.pbrMetallicRoughness.metallicRoughnessTexture.index, mMetallicRoughnessName);
    // get emissive texture
    LLPointer<LLImageRaw> emissive_img = LLTinyGLTFHelper::getTexture(folder, model_in, material_in.emissiveTexture.index, mEmissiveName);
    // get occlusion map if needed
    LLPointer<LLImageRaw> occlusion_img;
    if (material_in.occlusionTexture.index != material_in.pbrMetallicRoughness.metallicRoughnessTexture.index)
    {
        std::string tmp;
        occlusion_img = LLTinyGLTFHelper::getTexture(folder, model_in, material_in.occlusionTexture.index, tmp);
    }

    LLTinyGLTFHelper::initFetchedTextures(material_in, albedo_img, normal_img, mr_img, emissive_img, occlusion_img,
        mAlbedoFetched, mNormalFetched, mMetallicRoughnessFetched, mEmissiveFetched);
    pack_textures(albedo_img, normal_img, mr_img, emissive_img, occlusion_img,
        mAlbedoJ2C, mNormalJ2C, mMetallicRoughnessJ2C, mEmissiveJ2C);
    
    LLUUID albedo_id;
    if (mAlbedoFetched.notNull())
    {
        mAlbedoFetched->forceToSaveRawImage(0, F32_MAX);
        albedo_id = mAlbedoFetched->getID();
        
        if (mAlbedoName.empty())
        {
            mAlbedoName = MATERIAL_ALBEDO_DEFAULT_NAME;
        }
    }

    LLUUID normal_id;
    if (mNormalFetched.notNull())
    {
        mNormalFetched->forceToSaveRawImage(0, F32_MAX);
        normal_id = mNormalFetched->getID();

        if (mNormalName.empty())
        {
            mNormalName = MATERIAL_NORMAL_DEFAULT_NAME;
        }
    }

    LLUUID mr_id;
    if (mMetallicRoughnessFetched.notNull())
    {
        mMetallicRoughnessFetched->forceToSaveRawImage(0, F32_MAX);
        mr_id = mMetallicRoughnessFetched->getID();

        if (mMetallicRoughnessName.empty())
        {
            mMetallicRoughnessName = MATERIAL_METALLIC_DEFAULT_NAME;
        }
    }

    LLUUID emissive_id;
    if (mEmissiveFetched.notNull())
    {
        mEmissiveFetched->forceToSaveRawImage(0, F32_MAX);
        emissive_id = mEmissiveFetched->getID();

        if (mEmissiveName.empty())
        {
            mEmissiveName = MATERIAL_EMISSIVE_DEFAULT_NAME;
        }
    }

    setAlbedoId(albedo_id);
    setAlbedoUploadId(albedo_id);
    setMetallicRoughnessId(mr_id);
    setMetallicRoughnessUploadId(mr_id);
    setEmissiveId(emissive_id);
    setEmissiveUploadId(emissive_id);
    setNormalId(normal_id);
    setNormalUploadId(normal_id);

    setFromGltfModel(model_in);

    setFromGltfMetaData(filename_lc, model_in);

    setHasUnsavedChanges(true);
    openFloater();

    applyToSelection();
}

bool LLMaterialEditor::setFromGltfModel(tinygltf::Model& model, bool set_textures)
{
    if (model.materials.size() > 0)
    {
        tinygltf::Material& material_in = model.materials[0];

        if (set_textures)
        {
            S32 index;
            LLUUID id;

            // get albedo texture
            index = material_in.pbrMetallicRoughness.baseColorTexture.index;
            if (index >= 0)
            {
                id.set(model.images[index].uri);
                setAlbedoId(id);
            }
            else
            {
                setAlbedoId(LLUUID::null);
            }

            // get normal map
            index = material_in.normalTexture.index;
            if (index >= 0)
            {
                id.set(model.images[index].uri);
                setNormalId(id);
            }
            else
            {
                setNormalId(LLUUID::null);
            }

            // get metallic-roughness texture
            index = material_in.pbrMetallicRoughness.metallicRoughnessTexture.index;
            if (index >= 0)
            {
                id.set(model.images[index].uri);
                setMetallicRoughnessId(id);
            }
            else
            {
                setMetallicRoughnessId(LLUUID::null);
            }

            // get emissive texture
            index = material_in.emissiveTexture.index;
            if (index >= 0)
            {
                id.set(model.images[index].uri);
                setEmissiveId(id);
            }
            else
            {
                setEmissiveId(LLUUID::null);
            }
        }

        setAlphaMode(material_in.alphaMode);
        setAlphaCutoff(material_in.alphaCutoff);

        setAlbedoColor(LLTinyGLTFHelper::getColor(material_in.pbrMetallicRoughness.baseColorFactor));
        setEmissiveColor(LLTinyGLTFHelper::getColor(material_in.emissiveFactor));

        setMetalnessFactor(material_in.pbrMetallicRoughness.metallicFactor);
        setRoughnessFactor(material_in.pbrMetallicRoughness.roughnessFactor);

        setDoubleSided(material_in.doubleSided);
    }

    return true;
}

/**
 * Build a texture name from the contents of the (in tinyGLFT parlance) 
 * Image URI. This often is filepath to the original image on the users'
 *  local file system.
 */
const std::string LLMaterialEditor::getImageNameFromUri(std::string image_uri, const std::string texture_type)
{
    // getBaseFileName() works differently on each platform and file patchs 
    // can contain both types of delimiter so unify them then extract the 
    // base name (no path or extension)
    std::replace(image_uri.begin(), image_uri.end(), '\\', gDirUtilp->getDirDelimiter()[0]);
    std::replace(image_uri.begin(), image_uri.end(), '/', gDirUtilp->getDirDelimiter()[0]);
    const bool strip_extension = true;
    std::string stripped_uri = gDirUtilp->getBaseFileName(image_uri, strip_extension);

    // sometimes they can be really long and unwieldy - 64 chars is enough for anyone :) 
    const int max_texture_name_length = 64;
    if (stripped_uri.length() > max_texture_name_length)
    {
        stripped_uri = stripped_uri.substr(0, max_texture_name_length - 1);
    }

    // We intend to append the type of texture (albedo, emissive etc.) to the 
    // name of the texture but sometimes the creator already did that.  To try
    // to avoid repeats (not perfect), we look for the texture type in the name
    // and if we find it, do not append the type, later on. One way this fails
    // (and it's fine for now) is I see some texture/image uris have a name like
    // "metallic roughness" and of course, that doesn't match our predefined
    // name "metallicroughness" - consider fix later..
    bool name_includes_type = false;
    std::string stripped_uri_lower = stripped_uri;
    LLStringUtil::toLower(stripped_uri_lower);
    stripped_uri_lower.erase(std::remove_if(stripped_uri_lower.begin(), stripped_uri_lower.end(), isspace), stripped_uri_lower.end());
    std::string texture_type_lower = texture_type;
    LLStringUtil::toLower(texture_type_lower);
    texture_type_lower.erase(std::remove_if(texture_type_lower.begin(), texture_type_lower.end(), isspace), texture_type_lower.end());
    if (stripped_uri_lower.find(texture_type_lower) != std::string::npos)
    {
        name_includes_type = true;
    }

    // uri doesn't include the type at all
    if (name_includes_type == false)
    {
        // uri doesn't include the type and the uri is not empty
        // so we can include everything
        if (stripped_uri.length() > 0)
        {
            // example "DamagedHelmet: base layer (Albedo)"
            return STRINGIZE(
                mMaterialNameShort <<
                ": " <<
                stripped_uri <<
                " (" <<
                texture_type <<
                ")"
            );
        }
        else
        // uri doesn't include the type (because the uri is empty)
        // so we must reorganize the string a bit to include the name
        // and an explicit name type
        {
            // example "DamagedHelmet: (Emissive)"
            return STRINGIZE(
                mMaterialNameShort <<
                " (" <<
                texture_type <<
                ")"
            );
        }
    }
    else
    // uri includes the type so just use it directly with the
    // name of the material
    {
        return STRINGIZE(
            // example: AlienBust: normal_layer
            mMaterialNameShort <<
            ": " <<
            stripped_uri
        );
    }
}

/**
 * Update the metadata for the material based on what we find in the loaded
 * file (along with some assumptions and interpretations...). Fields include
 * the name of the material, a material description and the names of the 
 * composite textures.
 */
void LLMaterialEditor::setFromGltfMetaData(const std::string& filename, tinygltf::Model& model)
{
    // Use the name (without any path/extension) of the file that was 
    // uploaded as the base of the material name. Then if the name of the 
    // scene is present and not blank, append that and use the result as
    // the name of the material. This is a first pass at creating a 
    // naming scheme that is useful to real content creators and hopefully
    // avoid 500 materials in your inventory called "scene" or "Default"
    const bool strip_extension = true;
    std::string base_filename = gDirUtilp->getBaseFileName(filename, strip_extension);

    // Extract the name of the scene. Note it is often blank or some very
    // generic name like "Scene" or "Default" so using this in the name
    // is less useful than you might imagine.
    std::string scene_name;
    if (model.scenes.size() > 0)
    {
        tinygltf::Scene& scene_in = model.scenes[0];
        if (scene_in.name.length())
        {
            scene_name = scene_in.name;
        }
        else
        {
            // scene name is empty so no point using it
        }
    }
    else
    {
        // scene name isn't present so no point using it
    }

    // If we have a valid scene name, use it to build the short and 
    // long versions of the material name. The long version is used 
    // as you might expect, for the material name. The short version is
    // used as part of the image/texture name - the theory is that will 
    // allow content creators to track the material and the corresponding
    // textures
    if (scene_name.length())
    {
        mMaterialNameShort = base_filename;

        mMaterialName = STRINGIZE(
            base_filename << 
            " " << 
            "(" << 
            scene_name << 
            ")"
        );
    }
    else
    // otherwise, just use the trimmed filename as is
    {
        mMaterialNameShort = base_filename;
        mMaterialName = base_filename;
    }

    // sanitize the material name so that it's compatible with the inventory
    LLInventoryObject::correctInventoryName(mMaterialName);
    LLInventoryObject::correctInventoryName(mMaterialNameShort);

    // We also set the title of the floater to match the 
    // name of the material
    setTitle(mMaterialName);

    /**
     * Extract / derive the names of each composite texture. For each, the 
     * index in the first material (we only support 1 material currently) is
     * used to to determine which of the "Images" is used. If the index is -1
     * then that texture type is not present in the material (Seems to be 
     * quite common that a material is missing 1 or more types of texture)
     */
    if (model.materials.size() > 0)
    {
        const tinygltf::Material& first_material = model.materials[0];

        mAlbedoName = MATERIAL_ALBEDO_DEFAULT_NAME;
        // note: unlike the other textures, albedo doesn't have its own entry 
        // in the tinyGLTF Material struct. Rather, it is taken from a 
        // sub-texture in the pbrMetallicRoughness member
        int index = first_material.pbrMetallicRoughness.baseColorTexture.index;
        if (index > -1 && index < model.images.size())
        {
            // sanitize the name we decide to use for each texture
            std::string texture_name = getImageNameFromUri(model.images[index].uri, MATERIAL_ALBEDO_DEFAULT_NAME);
            LLInventoryObject::correctInventoryName(texture_name);
            mAlbedoName = texture_name;
        }

        mEmissiveName = MATERIAL_EMISSIVE_DEFAULT_NAME;
        index = first_material.emissiveTexture.index;
        if (index > -1 && index < model.images.size())
        {
            std::string texture_name = getImageNameFromUri(model.images[index].uri, MATERIAL_EMISSIVE_DEFAULT_NAME);
            LLInventoryObject::correctInventoryName(texture_name);
            mEmissiveName = texture_name;
        }

        mMetallicRoughnessName = MATERIAL_METALLIC_DEFAULT_NAME;
        index = first_material.pbrMetallicRoughness.metallicRoughnessTexture.index;
        if (index > -1 && index < model.images.size())
        {
            std::string texture_name = getImageNameFromUri(model.images[index].uri, MATERIAL_METALLIC_DEFAULT_NAME);
            LLInventoryObject::correctInventoryName(texture_name);
            mMetallicRoughnessName = texture_name;
        }

        mNormalName = MATERIAL_NORMAL_DEFAULT_NAME;
        index = first_material.normalTexture.index;
        if (index > -1 && index < model.images.size())
        {
            std::string texture_name = getImageNameFromUri(model.images[index].uri, MATERIAL_NORMAL_DEFAULT_NAME);
            LLInventoryObject::correctInventoryName(texture_name);
            mNormalName = texture_name;
        }
    }
}

void LLMaterialEditor::importMaterial()
{
    (new LLMaterialFilePicker())->getFile();
}

class LLRemderMaterialFunctor : public LLSelectedTEFunctor
{
public:
    LLRemderMaterialFunctor(LLGLTFMaterial *mat, const LLUUID &id)
        : mMat(mat), mMatId(id)
    {
    }

    virtual bool apply(LLViewerObject* objectp, S32 te)
    {
        if (objectp && objectp->permModify() && objectp->getVolume())
        {
            LLVOVolume* vobjp = (LLVOVolume*)objectp;
            vobjp->setRenderMaterialID(te, mMatId);
            vobjp->getTE(te)->setGLTFMaterial(mMat);
            vobjp->updateTEMaterialTextures(te);
        }
        return true;
    }
private:
    LLPointer<LLGLTFMaterial> mMat;
    LLUUID mMatId;
};

void LLMaterialEditor::applyToSelection()
{
    LLPointer<LLGLTFMaterial> mat = new LLGLTFMaterial();
    getGLTFMaterial(mat);
    const LLUUID placeholder("984e183e-7811-4b05-a502-d79c6f978a98");
    LLUUID asset_id = mAssetID.notNull() ? mAssetID : placeholder;
    LLRemderMaterialFunctor mat_func(mat, asset_id);
    LLObjectSelectionHandle selected_objects = LLSelectMgr::getInstance()->getSelection();
    selected_objects->applyToTEs(&mat_func);
}

void LLMaterialEditor::getGLTFMaterial(LLGLTFMaterial* mat)
{
    mat->mAlbedoColor = getAlbedoColor();
    mat->mAlbedoColor.mV[3] = getTransparency();
    mat->mAlbedoId = getAlbedoId();

    mat->mNormalId = getNormalId();

    mat->mMetallicRoughnessId = getMetallicRoughnessId();
    mat->mMetallicFactor = getMetalnessFactor();
    mat->mRoughnessFactor = getRoughnessFactor();

    mat->mEmissiveColor = getEmissiveColor();
    mat->mEmissiveId = getEmissiveId();

    mat->mDoubleSided = getDoubleSided();
    mat->setAlphaMode(getAlphaMode());
}

void LLMaterialEditor::setFromGLTFMaterial(LLGLTFMaterial* mat)
{
    setAlbedoColor(mat->mAlbedoColor);
    setAlbedoId(mat->mAlbedoId);
    setNormalId(mat->mNormalId);

    setMetallicRoughnessId(mat->mMetallicRoughnessId);
    setMetalnessFactor(mat->mMetallicFactor);
    setRoughnessFactor(mat->mRoughnessFactor);

    setEmissiveColor(mat->mEmissiveColor);
    setEmissiveId(mat->mEmissiveId);

    setDoubleSided(mat->mDoubleSided);
    setAlphaMode(mat->getAlphaMode());
}

void LLMaterialEditor::loadAsset()
{
    // derived from LLPreviewNotecard::loadAsset
    
    // TODO: see commented out "editor" references and make them do something appropriate to the UI
   
    // request the asset.
    const LLInventoryItem* item = getItem();
    
    bool fail = false;

    if (item)
    {
        LLPermissions perm(item->getPermissions());
        BOOL allow_copy = gAgent.allowOperation(PERM_COPY, perm, GP_OBJECT_MANIPULATE);
        BOOL allow_modify = canModify(mObjectUUID, item);
        BOOL source_library = mObjectUUID.isNull() && gInventory.isObjectDescendentOf(mItemUUID, gInventory.getLibraryRootFolderID());

        setCanSaveAs(allow_copy);
        setCanSave(allow_modify && !source_library);
        setMaterialName(item->getName());

        {
            mAssetID = item->getAssetUUID();
            if (mAssetID.isNull())
            {
                mAssetStatus = PREVIEW_ASSET_LOADED;
                loadDefaults();
                setHasUnsavedChanges(false);
            }
            else
            {
                LLHost source_sim = LLHost();
                LLSD* user_data = new LLSD();

                if (mObjectUUID.notNull())
                {
                    LLViewerObject* objectp = gObjectList.findObject(mObjectUUID);
                    if (objectp && objectp->getRegion())
                    {
                        source_sim = objectp->getRegion()->getHost();
                    }
                    else
                    {
                        // The object that we're trying to look at disappeared, bail.
                        LL_WARNS() << "Can't find object " << mObjectUUID << " associated with notecard." << LL_ENDL;
                        mAssetID.setNull();
                        mAssetStatus = PREVIEW_ASSET_LOADED;
                        setHasUnsavedChanges(false);
                        return;
                    }
                    user_data->with("taskid", mObjectUUID).with("itemid", mItemUUID);
                }
                else
                {
                    user_data = new LLSD(mItemUUID);
                }

                gAssetStorage->getInvItemAsset(source_sim,
                    gAgent.getID(),
                    gAgent.getSessionID(),
                    item->getPermissions().getOwner(),
                    mObjectUUID,
                    item->getUUID(),
                    item->getAssetUUID(),
                    item->getType(),
                    &onLoadComplete,
                    (void*)user_data,
                    TRUE);
                mAssetStatus = PREVIEW_ASSET_LOADING;
            }
        }
    }
    else if (mObjectUUID.notNull() && mItemUUID.notNull())
    {
        LLViewerObject* objectp = gObjectList.findObject(mObjectUUID);
        if (objectp && (objectp->isInventoryPending() || objectp->isInventoryDirty()))
        {
            // It's a material in object's inventory and we failed to get it because inventory is not up to date.
            // Subscribe for callback and retry at inventoryChanged()
            registerVOInventoryListener(objectp, NULL); //removes previous listener

            if (objectp->isInventoryDirty())
            {
                objectp->requestInventory();
            }
        }
        else
        {
            fail = true;
        }
    }
    else
    {
        fail = true;
    }

    if (fail)
    {
        /*editor->setText(LLStringUtil::null);
        editor->makePristine();
        editor->setEnabled(TRUE);*/
        // Don't set asset status here; we may not have set the item id yet
        // (e.g. when this gets called initially)
        //mAssetStatus = PREVIEW_ASSET_LOADED;
    }
}

// static
void LLMaterialEditor::onLoadComplete(const LLUUID& asset_uuid,
    LLAssetType::EType type,
    void* user_data, S32 status, LLExtStat ext_status)
{
    LL_INFOS() << "LLMaterialEditor::onLoadComplete()" << LL_ENDL;
    LLSD* floater_key = (LLSD*)user_data;
    LLMaterialEditor* editor = LLFloaterReg::findTypedInstance<LLMaterialEditor>("material_editor", *floater_key);
    if (editor)
    {
        if (0 == status)
        {
            LLFileSystem file(asset_uuid, type, LLFileSystem::READ);

            S32 file_length = file.getSize();

            std::vector<char> buffer(file_length + 1);
            file.read((U8*)&buffer[0], file_length);

            editor->decodeAsset(buffer);

            BOOL modifiable = editor->canModify(editor->mObjectID, editor->getItem());
            editor->setEnabled(modifiable);
            editor->setHasUnsavedChanges(false);
            editor->mAssetStatus = PREVIEW_ASSET_LOADED;
        }
        else
        {
            if (LL_ERR_ASSET_REQUEST_NOT_IN_DATABASE == status ||
                LL_ERR_FILE_EMPTY == status)
            {
                LLNotificationsUtil::add("MaterialMissing");
            }
            else if (LL_ERR_INSUFFICIENT_PERMISSIONS == status)
            {
                LLNotificationsUtil::add("MaterialNoPermissions");
            }
            else
            {
                LLNotificationsUtil::add("UnableToLoadMaterial");
            }

            LL_WARNS() << "Problem loading material: " << status << LL_ENDL;
            editor->mAssetStatus = PREVIEW_ASSET_ERROR;
        }
    }
    delete floater_key;
}

void LLMaterialEditor::inventoryChanged(LLViewerObject* object,
    LLInventoryObject::object_list_t* inventory,
    S32 serial_num,
    void* user_data)
{
    removeVOInventoryListener();
    loadAsset();
}


void LLMaterialEditor::saveTexture(LLImageJ2C* img, const std::string& name, const LLUUID& asset_id, upload_callback_f cb)
{
    if (asset_id.isNull()
        || img == nullptr
        || img->getDataSize() == 0)
    {
        return;
    }

    // copy image bytes into string
    std::string buffer;
    buffer.assign((const char*) img->getData(), img->getDataSize());

    U32 expected_upload_cost = LLAgentBenefitsMgr::current().getTextureUploadCost();


    LLResourceUploadInfo::ptr_t uploadInfo(std::make_shared<LLNewBufferedResourceUploadInfo>(
        buffer,
        asset_id,
        name, 
        name, 
        0,
        LLFolderType::FT_TEXTURE, 
        LLInventoryType::IT_TEXTURE,
        LLAssetType::AT_TEXTURE,
        LLFloaterPerms::getNextOwnerPerms("Uploads"),
        LLFloaterPerms::getGroupPerms("Uploads"),
        LLFloaterPerms::getEveryonePerms("Uploads"),
        expected_upload_cost, 
        false,
        cb));

    upload_new_resource(uploadInfo);
}

S32 LLMaterialEditor::saveTextures()
{
    S32 work_count = 0;
    LLSD key = getKey(); // must be locally declared for lambda's capture to work
    if (mAlbedoTextureUploadId == getAlbedoId() && mAlbedoTextureUploadId.notNull())
    {
        mUploadingTexturesCount++;
        work_count++;
        saveTexture(mAlbedoJ2C, mAlbedoName, mAlbedoTextureUploadId, [key](LLUUID newAssetId, LLSD response)
        {
            LLMaterialEditor* me = LLFloaterReg::findTypedInstance<LLMaterialEditor>("material_editor", key);
            if (me)
            {
                if (response["success"].asBoolean())
                {
                    me->setAlbedoId(newAssetId);
                }
                else
                {
                    // To make sure that we won't retry (some failures can cb immediately)
                    me->setAlbedoId(LLUUID::null);
                }
                me->mUploadingTexturesCount--;

                // try saving
                me->saveIfNeeded();
            }
        });
    }
    if (mNormalTextureUploadId == getNormalId() && mNormalTextureUploadId.notNull())
    {
        mUploadingTexturesCount++;
        work_count++;
        saveTexture(mNormalJ2C, mNormalName, mNormalTextureUploadId, [key](LLUUID newAssetId, LLSD response)
        {
            LLMaterialEditor* me = LLFloaterReg::findTypedInstance<LLMaterialEditor>("material_editor", key);
            if (me)
            {
                if (response["success"].asBoolean())
                {
                    me->setNormalId(newAssetId);
                }
                else
                {
                    me->setNormalId(LLUUID::null);
                }
                me->setNormalId(newAssetId);
                me->mUploadingTexturesCount--;

                // try saving
                me->saveIfNeeded();
            }
        });
    }
    if (mMetallicTextureUploadId == getMetallicRoughnessId() && mMetallicTextureUploadId.notNull())
    {
        mUploadingTexturesCount++;
        work_count++;
        saveTexture(mMetallicRoughnessJ2C, mMetallicRoughnessName, mMetallicTextureUploadId, [key](LLUUID newAssetId, LLSD response)
        {
            LLMaterialEditor* me = LLFloaterReg::findTypedInstance<LLMaterialEditor>("material_editor", key);
            if (me)
            {
                if (response["success"].asBoolean())
                {
                    me->setMetallicRoughnessId(newAssetId);
                }
                else
                {
                    me->setMetallicRoughnessId(LLUUID::null);
                }
                me->mUploadingTexturesCount--;

                // try saving
                me->saveIfNeeded();
            }
        });
    }

    if (mEmissiveTextureUploadId == getEmissiveId() && mEmissiveTextureUploadId.notNull())
    {
        mUploadingTexturesCount++;
        work_count++;
        saveTexture(mEmissiveJ2C, mEmissiveName, mEmissiveTextureUploadId, [key](LLUUID newAssetId, LLSD response)
        {
            LLMaterialEditor* me = LLFloaterReg::findTypedInstance<LLMaterialEditor>("material_editor", LLSD(key));
            if (me)
            {
                if (response["success"].asBoolean())
                {
                    me->setEmissiveId(newAssetId);
                }
                else
                {
                    me->setEmissiveId(LLUUID::null);
                }
                me->mUploadingTexturesCount--;

                // try saving
                me->saveIfNeeded();
            }
        });
    }

    // discard upload buffers once textures have been saved
    mAlbedoJ2C = nullptr;
    mNormalJ2C = nullptr;
    mEmissiveJ2C = nullptr;
    mMetallicRoughnessJ2C = nullptr;

    mAlbedoFetched = nullptr;
    mNormalFetched = nullptr;
    mMetallicRoughnessFetched = nullptr;
    mEmissiveFetched = nullptr;

    mAlbedoTextureUploadId.setNull();
    mNormalTextureUploadId.setNull();
    mMetallicTextureUploadId.setNull();
    mEmissiveTextureUploadId.setNull();

    // asset storage can callback immediately, causing a decrease
    // of mUploadingTexturesCount, report amount of work scheduled
    // not amount of work remaining
    return work_count;
}

void LLMaterialEditor::loadDefaults()
{
    tinygltf::Model model_in;
    model_in.materials.resize(1);
    setFromGltfModel(model_in, true);
}