//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftooloptimize.h"
#include "pdfdocumentwriter.h"


namespace pdftool
{

static PDFToolOptimize s_optimizeApplication;

QString PDFToolOptimize::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "optimize";

        case Name:
            return PDFToolTranslationContext::tr("Optimize");

        case Description:
            return PDFToolTranslationContext::tr("Optimize document size using various algorithms.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolOptimize::execute(const PDFToolOptions& options)
{
    if (!options.optimizeFlags)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("No optimization option has been set."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData))
    {
        return ErrorDocumentReading;
    }

    pdf::PDFOptimizer optimizer(options.optimizeFlags, nullptr);
    QObject::connect(&optimizer, &pdf::PDFOptimizer::optimizationProgress, &optimizer, [&options](QString text) { PDFConsole::writeError(text, options.outputCodec); }, Qt::DirectConnection);
    optimizer.setDocument(&document);
    optimizer.optimize();
    document = optimizer.takeOptimizedDocument();

    pdf::PDFDocumentWriter writer(nullptr);
    pdf::PDFOperationResult result = writer.write(options.document, &document, true);
    if (!result)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to write optimize document. %1").arg(result.getErrorMessage()), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolOptimize::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | Optimize;
}

}   // namespace pdftool