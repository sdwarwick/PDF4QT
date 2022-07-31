//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pageitemdelegate.h"
#include "pageitemmodel.h"
#include "pdfwidgetutils.h"
#include "pdfpainterutils.h"
#include "pdfrenderer.h"
#include "pdfcompiler.h"
#include "pdfconstants.h"

#include <QPainter>
#include <QPixmapCache>

namespace pdfdocpage
{

PageItemDelegate::PageItemDelegate(PageItemModel* model, QObject* parent) :
    BaseClass(parent),
    m_model(model),
    m_rasterizer(nullptr)
{
    m_rasterizer = new pdf::PDFRasterizer(this);
    QSurfaceFormat format;
    format.setSamples(16);
    m_rasterizer->reset(true, format);
}

PageItemDelegate::~PageItemDelegate()
{

}

void PageItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const PageGroupItem* item = m_model->getItem(index);

    if (!item)
    {
        return;
    }

    QRect rect = option.rect;

    QSize scaledSize = pdf::PDFWidgetUtils::scaleDPI(option.widget, m_pageImageSize);
    int verticalSpacing = pdf::PDFWidgetUtils::scaleDPI_y(option.widget, getVerticalSpacing());
    int horizontalSpacing = pdf::PDFWidgetUtils::scaleDPI_x(option.widget, getHorizontalSpacing());

    QRect pageBoundingRect = QRect(QPoint(rect.left() + (rect.width() - scaledSize.width()) / 2, rect.top() + verticalSpacing), scaledSize);

    // Draw page preview
    if (!item->groups.empty())
    {
        const PageGroupItem::GroupItem& groupItem = item->groups.front();
        QSizeF rotatedPageSize = pdf::PDFPage::getRotatedBox(QRectF(QPointF(0, 0), groupItem.rotatedPageDimensionsMM), groupItem.pageAdditionalRotation).size();
        QSize pageImageSize = rotatedPageSize.scaled(pageBoundingRect.size(), Qt::KeepAspectRatio).toSize();
        QRect pageImageRect(pageBoundingRect.topLeft() + QPoint((pageBoundingRect.width() - pageImageSize.width()) / 2, (pageBoundingRect.height() - pageImageSize.height()) / 2), pageImageSize);

        painter->setBrush(Qt::white);
        painter->drawRect(pageImageRect);

        QPixmap pageImagePixmap = getPageImagePixmap(item, pageImageRect);
        if (!pageImagePixmap.isNull())
        {
            painter->drawPixmap(pageImageRect, pageImagePixmap);
        }

        painter->setPen(QPen(Qt::black));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(pageImageRect);
    }

    int textOffset = pageBoundingRect.bottom() + verticalSpacing;
    QRect textRect = option.rect;
    textRect.setTop(textOffset);
    textRect.setHeight(option.fontMetrics.lineSpacing());
    painter->drawText(textRect, Qt::AlignCenter | Qt::TextSingleLine, item->groupName);
    textRect.translate(0, textRect.height());
    painter->drawText(textRect, Qt::AlignCenter | Qt::TextSingleLine, item->pagesCaption);

    if (option.state.testFlag(QStyle::State_Selected))
    {
        QColor selectedColor = option.palette.color(QPalette::Active, QPalette::Highlight);
        selectedColor.setAlphaF(0.3);
        painter->fillRect(rect, selectedColor);
    }

    QPoint tagPoint = rect.topRight() + QPoint(-horizontalSpacing, verticalSpacing);
    for (const QString& tag : item->tags)
    {
        QStringList splitted = tag.split('@', Qt::KeepEmptyParts);
        if (splitted.size() != 2 || splitted.back().isEmpty())
        {
            continue;
        }

        QColor color;
        color.setNamedColor(splitted.front());
        QRect bubbleRect = pdf::PDFPainterHelper::drawBubble(painter, tagPoint, color, splitted.back(), Qt::AlignLeft | Qt::AlignBottom);
        tagPoint.ry() += bubbleRect.height() + verticalSpacing;
    }
}

QSize PageItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    Q_UNUSED(index);

    QSize scaledSize = pdf::PDFWidgetUtils::scaleDPI(option.widget, m_pageImageSize);
    int height = scaledSize.height() + option.fontMetrics.lineSpacing() * 2 + 2 * pdf::PDFWidgetUtils::scaleDPI_y(option.widget, getVerticalSpacing());
    int width = qMax(pdf::PDFWidgetUtils::scaleDPI_x(option.widget, 40), scaledSize.width() + 2 * pdf::PDFWidgetUtils::scaleDPI_x(option.widget, getHorizontalSpacing()));
    return QSize(width, height);
}

QSize PageItemDelegate::getPageImageSize() const
{
    return m_pageImageSize;
}

void PageItemDelegate::setPageImageSize(QSize pageImageSize)
{
    if (m_pageImageSize != pageImageSize)
    {
        m_pageImageSize = pageImageSize;
        emit sizeHintChanged(QModelIndex());
    }
}

QPixmap PageItemDelegate::getPageImagePixmap(const PageGroupItem* item, QRect rect) const
{
    QPixmap pixmap;

    Q_ASSERT(item);
    if (item->groups.empty())
    {
        return pixmap;
    }

    const PageGroupItem::GroupItem& groupItem = item->groups.front();
    if (groupItem.pageType == PT_Empty)
    {
        return pixmap;
    }

    // Jakub Melka: generate key and see, if pixmap is not cached
    QString key = QString("%1#%2#%3#%4#%5@%6x%7").arg(groupItem.documentIndex).arg(groupItem.imageIndex).arg(int(groupItem.pageAdditionalRotation)).arg(groupItem.pageIndex).arg(groupItem.pageType).arg(rect.width()).arg(rect.height());

    if (!QPixmapCache::find(key, &pixmap))
    {
        // We must draw the pixmap
        pixmap = QPixmap(rect.width(), rect.height());
        pixmap.fill(Qt::transparent);

        switch (groupItem.pageType)
        {
            case pdfdocpage::PT_DocumentPage:
            {
                const auto& documents = m_model->getDocuments();
                auto it = documents.find(groupItem.documentIndex);
                if (it != documents.cend())
                {
                    const pdf::PDFDocument& document = it->second.document;
                    const pdf::PDFInteger pageIndex = groupItem.pageIndex - 1;
                    if (pageIndex >= 0 && pageIndex < pdf::PDFInteger(document.getCatalog()->getPageCount()))
                    {
                        const pdf::PDFPage* page = document.getCatalog()->getPage(pageIndex);
                        Q_ASSERT(page);

                        pdf::PDFPrecompiledPage compiledPage;
                        pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
                        pdf::PDFCMSManager cmsManager(nullptr);
                        pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::View, nullptr);

                        fontCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(&document), &optionalContentActivity));
                        cmsManager.setDocument(&document);

                        pdf::PDFCMSPointer cms = cmsManager.getCurrentCMS();
                        pdf::PDFRenderer renderer(&document, &fontCache, cms.data(), &optionalContentActivity, pdf::PDFRenderer::getDefaultFeatures(), pdf::PDFMeshQualitySettings());
                        renderer.compile(&compiledPage, pageIndex);

                        QSize imageSize = rect.size();
                        QImage pageImage = m_rasterizer->render(pageIndex, page, &compiledPage, imageSize, pdf::PDFRenderer::getDefaultFeatures(), nullptr, groupItem.pageAdditionalRotation);
                        pixmap = QPixmap::fromImage(qMove(pageImage));
                    }
                }
                break;
            }

            case pdfdocpage::PT_Image:
            {
                const auto& images = m_model->getImages();
                auto it = images.find(groupItem.imageIndex);
                if (it != images.cend())
                {
                    const QImage& image = it->second.image;
                    if (!image.isNull())
                    {
                        QRect drawRect(QPoint(0, 0), rect.size());
                        QRect mediaBox(QPoint(0, 0), image.size());
                        QRectF rotatedMediaBox = pdf::PDFPage::getRotatedBox(mediaBox, groupItem.pageAdditionalRotation);
                        QMatrix matrix = pdf::PDFRenderer::createMediaBoxToDevicePointMatrix(rotatedMediaBox, drawRect, groupItem.pageAdditionalRotation);

                        QPainter painter(&pixmap);
                        painter.setWorldTransform(QTransform(matrix));
                        painter.translate(0, image.height());
                        painter.scale(1.0, -1.0);
                        painter.drawImage(0, 0, image);
                    }
                }
                break;
            }

            case pdfdocpage::PT_Empty:
                Q_ASSERT(false);
                break;

            default:
                Q_ASSERT(false);
                break;
        }

        QPixmapCache::insert(key, pixmap);
    }

    return pixmap;
}

}   // namespace pdfdocpage
