/*****************************************************************************
 * Copyright (C) 2021 VLC authors and VideoLAN
 *
 * Authors: Benjamin Arnaud <bunjee@omega.gg>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "mlvideogroupsmodel.hpp"

// VLC includes
#include <vlc_media_library.h>

// Util includes
#include "util/covergenerator.hpp"
#include "util/vlctick.hpp"

// MediaLibrary includes
#include "mlcustomcover.hpp"
#include "mlgroup.hpp"
#include "mlvideo.hpp"
#include "mlhelper.hpp"

//-------------------------------------------------------------------------------------------------
// Static variables

// NOTE: We multiply by 3 to cover most dpi settings.
static const int MLVIDEOGROUPSMODEL_COVER_WIDTH  = 260 * 3; // 16 / 10 ratio
static const int MLVIDEOGROUPSMODEL_COVER_HEIGHT = 162 * 3;

//=================================================================================================
// MLVideoGroupsModel
//=================================================================================================

/* explicit */ MLVideoGroupsModel::MLVideoGroupsModel(QObject * parent) : MLVideoModel(parent) {}

//-------------------------------------------------------------------------------------------------
// MLVideoModel reimplementation
//-------------------------------------------------------------------------------------------------

QHash<int, QByteArray> MLVideoGroupsModel::roleNames() const /* override */
{
    QHash<int, QByteArray> hash = MLVideoModel::roleNames();

    const QHash<int, QByteArray> groupRoles =
    {
        {GROUP_IS_VIDEO, "isVideo"},
        {GROUP_TITLE_FIRST_SYMBOL, "group_title_first_symbol"},
        {GROUP_DATE, "date"},
        {GROUP_COUNT, "count"},
    };

    hash.insert(groupRoles);

    return hash;
}

// Protected MLVideoModel implementation

QVariant MLVideoGroupsModel::itemRoleData(const MLItem * item, const int role) const /* override */
{
    if (item == nullptr)
        return QVariant();

    if (item->getId().type == VLC_ML_PARENT_GROUP)
    {
        auto group = static_cast<const MLGroup *> (item);

        switch (role)
        {
            // NOTE: This is the condition for QWidget view(s).
            case Qt::DisplayRole:
                return QVariant::fromValue(group->getTitle());
            // NOTE: These are the conditions for QML view(s).
            case MEDIA_ID:
                return QVariant::fromValue(group->getId());
            case MEDIA_TITLE:
                return QVariant::fromValue(group->getTitle());
            case VIDEO_THUMBNAIL:
            {
                return MLCustomCover::url(
                    group->getId(),
                    QSize(MLVIDEOGROUPSMODEL_COVER_WIDTH, MLVIDEOGROUPSMODEL_COVER_HEIGHT),
                    QStringLiteral(":/placeholder/noart_videoCover.svg")
                );
            }
            case MEDIA_DURATION:
                return QVariant::fromValue(group->getDuration());
            case GROUP_IS_VIDEO:
                return false;
            case GROUP_TITLE_FIRST_SYMBOL:
                return QVariant::fromValue( getFirstSymbol(group->getTitle() ));
            case GROUP_DATE:
                return QVariant::fromValue(group->getDate());
            case GROUP_COUNT:
                return QVariant::fromValue(group->getCount());
            default:
                return QVariant();
        }
    }
    else
    {
        auto video = static_cast<const MLVideo *> (item);

        switch (role)
        {
            case Qt::DisplayRole:
                return QVariant::fromValue(video->title());
            case GROUP_TITLE_FIRST_SYMBOL:
                // videos and groups are shown mixed, force this item into a group
                // for grouping the data must be sorted by title
                return QVariant::fromValue( getFirstSymbol(video->title() ));
            case GROUP_IS_VIDEO:
                return true;
            case GROUP_DATE:
                return QVariant();
            case GROUP_COUNT:
                return 1;
            // NOTE: Media specific.
            default:
                return MLVideoModel::itemRoleData(item, role);
        }
    }
}

vlc_ml_sorting_criteria_t MLVideoGroupsModel::nameToCriteria(QByteArray name) const /* override */
{
    return QHash<QByteArray, vlc_ml_sorting_criteria_t> {
        { "title",    VLC_ML_SORTING_ALPHA         },
        { "duration", VLC_ML_SORTING_DURATION      },
        { "date",     VLC_ML_SORTING_INSERTIONDATE },
    }.value(name, VLC_ML_SORTING_DEFAULT);
}

std::unique_ptr<MLListCacheLoader> MLVideoGroupsModel::createMLLoader() const /* override */
{
    return std::make_unique<MLListCacheLoader>(m_mediaLib, std::make_shared<MLVideoGroupsModel::Loader>(*this));
}

void MLVideoGroupsModel::onVlcMlEvent(const MLEvent & event) /* override */
{
    int type = event.i_type;

    switch (type)
    {
    case VLC_ML_EVENT_GROUP_ADDED:
    {
        emit resetRequested();
        return;
    }
    case VLC_ML_EVENT_MEDIA_ADDED:
    {
        if (event.creation.media.i_type == VLC_ML_MEDIA_TYPE_VIDEO)
        {
            emit resetRequested();
            return;
        }
        break;
    }
    case VLC_ML_EVENT_GROUP_UPDATED:
    {
        MLItemId itemId(event.modification.i_entity_id, VLC_ML_PARENT_GROUP);
        updateItemInCache(itemId);
        return;
    }
    case VLC_ML_EVENT_MEDIA_UPDATED:
    {
        MLItemId itemId(event.modification.i_entity_id, VLC_ML_PARENT_UNKNOWN);
        updateItemInCache(itemId);
        return;
    }
    case VLC_ML_EVENT_GROUP_DELETED:
    {
        MLItemId itemId(event.deletion.i_entity_id, VLC_ML_PARENT_GROUP);
        deleteItemInCache(itemId);
        return;
    }
    case VLC_ML_EVENT_MEDIA_DELETED:
    {
        MLItemId itemId(event.deletion.i_entity_id, VLC_ML_PARENT_UNKNOWN);
        deleteItemInCache(itemId);
        return;
    }
    default:
        break;
    }

    MLBaseModel::onVlcMlEvent(event);
}

//=================================================================================================
// Loader
//=================================================================================================

size_t MLVideoGroupsModel::Loader::count(vlc_medialibrary_t* ml, const vlc_ml_query_params_t* queryParams) const /* override */
{
    return vlc_ml_count_groups(ml, queryParams);
}

std::vector<std::unique_ptr<MLItem>>
MLVideoGroupsModel::Loader::load(vlc_medialibrary_t* ml,
                                const vlc_ml_query_params_t* queryParams) const /* override */
{
    ml_unique_ptr<vlc_ml_group_list_t> list(vlc_ml_list_groups(ml, queryParams));

    if (list == nullptr)
        return {};

    std::vector<std::unique_ptr<MLItem>> result;

    for (const vlc_ml_group_t & group : ml_range_iterate<vlc_ml_group_t>(list))
    {
        // NOTE: When it's a group of one we convert it to a MLVideo.
        if (group.i_nb_total_media == 1)
        {
            vlc_ml_query_params_t query = vlc_ml_query_params_create();

            ml_unique_ptr<vlc_ml_media_list_t> list(vlc_ml_list_group_media(ml,
                                                                            &query, group.i_id));

            // NOTE: Do we really need to check 'i_nb_items' here ?
            if (list->i_nb_items == 1)
            {
                result.emplace_back(std::make_unique<MLVideo>(&(list->p_items[0])));

                continue;
            }
        }

        result.emplace_back(std::make_unique<MLGroup>(&group));
    }

    return result;
}


std::unique_ptr<MLItem>
MLVideoGroupsModel::Loader::loadItemById(vlc_medialibrary_t* ml, MLItemId itemId) const
{
    if (itemId.type == VLC_ML_PARENT_UNKNOWN)
    {
        ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media(ml, itemId.id));
        if (!media)
            return nullptr;
        return std::make_unique<MLVideo>(media.get());
    }
    else if (itemId.type == VLC_ML_PARENT_GROUP)
    {
        ml_unique_ptr<vlc_ml_group_t> group(vlc_ml_get_group(ml, itemId.id));
        if (!group)
            return nullptr;
        return std::make_unique<MLGroup>(group.get());
    }
    else
    {
        vlc_assert_unreachable();
        return nullptr;
    }

}
