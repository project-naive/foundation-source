/*
 * $Id$
 * Copyright (c) 2008-2010, Matroska (non-profit organisation)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Matroska assocation nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY the Matroska association ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL The Matroska Foundation BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ebml/ebml2.h"
#include "ebml/ebml_internal.h"
#include "matroska/matroska_sem.h"
#include "matroska/matroska_internal.h"
#include "matroska/matroska2.h"
#include "matroska/MatroskaParser.h"
#include <ebml/corec/err.h>
#define MAX_TRACKS 32 // safety

#define MAXLINE 1024
#include <string.h>
#include <stdio.h>
#if defined(TARGET_WIN)
#define snprintf _snprintf
#endif

#define HAALI_STREAM_CLASS FOURCC('H','A','L','S')
typedef struct haali_stream {
	stream Base;
	InputStream* io;
	int io_flag;
} haali_stream;

void mkv_SetTrackMask(MatroskaFile *File, int Mask)
{
	File->trackMask = Mask;
	// TODO: the original code is handling a queue
}

void mkv_GetTags(MatroskaFile *File, Tag **pTags, unsigned *Count)
{
	*pTags = ARRAYBEGIN(File->Tags,Tag);
	*Count = ARRAYCOUNT(File->Tags,Tag);
}

void mkv_GetAttachments(MatroskaFile *File, Attachment **pAttachements, unsigned *Count)
{
	*pAttachements = ARRAYBEGIN(File->Attachments,Attachment);
	*Count = ARRAYCOUNT(File->Attachments,Attachment);
}

void mkv_GetChapters(MatroskaFile *File, Chapter **pChapters, unsigned *Count)
{
	*pChapters = ARRAYBEGIN(File->Chapters,Chapter);
	*Count = ARRAYCOUNT(File->Chapters,Chapter);
}

TrackInfo *mkv_GetTrackInfo(MatroskaFile *File, size_t n)
{
	if (n>=ARRAYCOUNT(File->Tracks,TrackInfo))
		return NULL;
	return ARRAYBEGIN(File->Tracks,TrackInfo) + n;
}

SegmentInfo *mkv_GetFileInfo(MatroskaFile *File)
{
	return &File->Seg;
}

size_t mkv_GetNumTracks(MatroskaFile *File)
{
	return ARRAYCOUNT(File->Tracks,TrackInfo);
}

static bool_t CheckMatroskaHead(MatroskaFile* File, ebml_element *Head, int* UpperElement, char *err_msg, size_t err_msgSize)
{
	ebml_parser_context SubContext;
	ebml_element* SubElement;
	tchar_t String[MAXLINE];

	SubContext.UpContext = &File->L0Context;
	SubContext.Context = EBML_ElementContext(Head);
	SubContext.EndPosition = EBML_ElementPositionEnd(Head);
	SubContext.Profile = File->L0Context.Profile;
	SubElement = EBML_FindNextElement(File->IOStream, &SubContext, &UpperElement, 1);

	while (SubElement) {
		if (EBML_ElementIsType(SubElement, &EBML_ContextReadVersion)) {
			if (EBML_ElementReadData(SubElement, File->IOStream, NULL, 0, SCOPE_ALL_DATA, 0) != ERR_NONE) {
				strcpy(err_msg, "Error reading EBML_ContextReadVersion\n");
				NodeDelete((node*)SubElement);
				return 0;
			}
			else if (EBML_IntegerValue((ebml_integer*)SubElement) > EBML_MAX_VERSION) {
				sprintf(err_msg, "EBML Read version %ld not supported.\n", EBML_IntegerValue((ebml_integer*)SubElement));
				NodeDelete((node*)SubElement);
				return 0;
			}
		}
		else if (EBML_ElementIsType(SubElement, &EBML_ContextMaxIdLength)) {
			if (EBML_ElementReadData(SubElement, File->IOStream, NULL, 0, SCOPE_ALL_DATA, 0) != ERR_NONE) {
				sprintf(err_msg, "Error reading EBML_ContextMaxIdLength.\n");
				NodeDelete((node*)SubElement);
				return 0;
			}
			else if (EBML_IntegerValue((ebml_integer*)SubElement) > EBML_MAX_ID) {
				sprintf(err_msg, "EBML Max ID Length %ld not supported.\n", EBML_IntegerValue((ebml_integer*)SubElement));
				NodeDelete((node*)SubElement);
				return 0;
			}
		}
		else if (EBML_ElementIsType(SubElement, &EBML_ContextMaxSizeLength)) {
			if (EBML_ElementReadData(SubElement, File->IOStream, NULL, 0, SCOPE_ALL_DATA, 0) != ERR_NONE) {
				sprintf(err_msg, "Error reading EBML_ContextMaxSizeLength.\n");
				NodeDelete((node*)SubElement);
				return 0;
			}
			else if (EBML_IntegerValue((ebml_integer*)SubElement) > EBML_MAX_SIZE) {
				sprintf(err_msg, "EBML Max Coded Size %ld not supported.\n", EBML_IntegerValue((ebml_integer*)SubElement));
				NodeDelete((node*)SubElement);
				return 0;
			}
		}
		else if (EBML_ElementIsType(SubElement, &EBML_ContextDocType)) {
			if (EBML_ElementReadData(SubElement, File->IOStream, NULL, 0, SCOPE_ALL_DATA, 0) != ERR_NONE) {
				sprintf(err_msg, "Error reading EBML_ContextDocType.\n");
				NodeDelete((node*)SubElement);
				break;
			}
			else {
				EBML_StringGet((ebml_string*)SubElement, String, TSIZEOF(String));
				if (strcmp(String, "matroska") == 0)
					File->Profile = PROFILE_MATROSKA_V1;
				else if (strcmp(String, "webm") == 0)
					File->Profile = PROFILE_WEBM;
				else {
					sprintf(err_msg, "EBML DocType '%s' not supported.\n", String);
					NodeDelete((node*)SubElement);
					return 0;
				}
			}
		}
		else if (EBML_ElementIsType(SubElement, &EBML_ContextDocTypeReadVersion)) {
			if (EBML_ElementReadData(SubElement, File->IOStream, NULL, 0, SCOPE_ALL_DATA, 0) != ERR_NONE) {
				sprintf(err_msg, "Error reading EBML_ContextDocTypeReadVersion.\n");
				NodeDelete((node*)SubElement);
				return 0;
			}
			else if (EBML_IntegerValue((ebml_integer*)SubElement) > MATROSKA_VERSION) {
				sprintf(err_msg, "EBML Read version %ld not supported.\n", EBML_IntegerValue((ebml_integer*)SubElement));
				NodeDelete((node*)SubElement);
				return 0;
			}
			else
				File->DocVersion = (int)EBML_IntegerValue((ebml_integer*)SubElement);
		}
		else if (EBML_ElementIsType(SubElement, &MATROSKA_ContextSegment)) {
			File->Segment = SubElement;
			return 1;
		}
		else
			EBML_ElementSkipData(SubElement, File->IOStream, NULL, NULL, 0);
		NodeDelete((node*)SubElement);
		SubElement = EBML_FindNextElement(File->IOStream, &SubContext, &UpperElement, 1);
	}
	return 1;
}

static bool_t parseSeekHead(ebml_element *SeekHead, MatroskaFile *File, char *err_msg, size_t err_msgSize)
{
	ebml_parser_context RContext;
	ebml_element *Elt,*EltId;
	fourcc_t EltID;
	filepos_t SegStart = EBML_ElementPositionData(File->Segment);
	assert(SegStart!=INVALID_FILEPOS_T);

	RContext.Context = SeekHead->Context;
	if (EBML_ElementIsFiniteSize(SeekHead))
		RContext.EndPosition = EBML_ElementPositionEnd(SeekHead);
	else
		RContext.EndPosition = INVALID_FILEPOS_T;
	RContext.UpContext = &File->L1Context;
	if (EBML_ElementReadData(SeekHead,(stream*)File->IOStream,&RContext,1,SCOPE_ALL_DATA,0)!=ERR_NONE)
	{
		strncpy(err_msg,"Failed to read the EBML head",err_msgSize);
		return 0;
	}

	Elt = EBML_MasterFindFirstElt(SeekHead,&MATROSKA_ContextSeek,0,0);
	while (Elt)
	{
		EltId = EBML_MasterFindFirstElt(Elt,&MATROSKA_ContextSeekID,0,0);
		if (EltId && EltId->DataSize > EBML_MAX_ID)
		{
			snprintf(err_msg,err_msgSize,"Invalid ID size in parseSeekEntry: %d",(int)EltId->DataSize);
			return 0;
		}
		EltID = MATROSKA_MetaSeekID((matroska_seekpoint *)Elt);
		if (EltID == MATROSKA_ContextInfo.Id)
			File->pSegmentInfo = MATROSKA_MetaSeekPosInSegment((matroska_seekpoint *)Elt) + SegStart;
		else if (EltID == MATROSKA_ContextTracks.Id)
			File->pTracks = MATROSKA_MetaSeekPosInSegment((matroska_seekpoint *)Elt) + SegStart;
		else if (EltID == MATROSKA_ContextCues.Id)
			File->pCues = MATROSKA_MetaSeekPosInSegment((matroska_seekpoint *)Elt) + SegStart;
		else if (EltID == MATROSKA_ContextAttachments.Id)
			File->pAttachments = MATROSKA_MetaSeekPosInSegment((matroska_seekpoint *)Elt) + SegStart;
		else if (EltID == MATROSKA_ContextChapters.Id)
			File->pChapters = MATROSKA_MetaSeekPosInSegment((matroska_seekpoint *)Elt) + SegStart;
		else if (EltID == MATROSKA_ContextTags.Id)
			File->pTags = MATROSKA_MetaSeekPosInSegment((matroska_seekpoint *)Elt) + SegStart;
		Elt = EBML_MasterFindNextElt(SeekHead,Elt,0,0);
	}

	return 1;
}

static bool_t parseSegmentInfo(ebml_element *Segment_Info, MatroskaFile *File, char *err_msg, size_t err_msgSize)
{
	ebml_parser_context RContext;
	ebml_element *Elt;
	double duration = -1.0;

	RContext.Context = Segment_Info->Context;
	if (EBML_ElementIsFiniteSize(Segment_Info))
		RContext.EndPosition = EBML_ElementPositionEnd(Segment_Info);
	else
		RContext.EndPosition = INVALID_FILEPOS_T;
	RContext.UpContext = &File->L1Context;
	if (EBML_ElementReadData(Segment_Info,(stream*)File->IOStream,&RContext,1,SCOPE_ALL_DATA,0)!=ERR_NONE)
	{
		strncpy(err_msg,"Failed to read the Segment Info",err_msgSize);
		File->pSegmentInfo = INVALID_FILEPOS_T;
		return 0;
	}
	File->pSegmentInfo = Segment_Info->ElementPosition;
	File->Segment_Info = Segment_Info;
	File->Seg.TimestampScale = MATROSKA_ContextTimestampScale.DefaultValue;

	for (Elt = EBML_MasterChildren(Segment_Info);Elt;Elt = EBML_MasterNext(Elt))
	{
		if (Elt->Context->Id == MATROSKA_ContextTimestampScale.Id)
		{
			File->Seg.TimestampScale = EBML_IntegerValue(Elt);
			if (File->Seg.TimestampScale==0)
			{
				strncpy(err_msg,"Segment timecode scale is zero",err_msgSize);
				return 0;
			}
		}
		else if (Elt->Context->Id == MATROSKA_ContextDuration.Id)
		{
			duration = ((ebml_float*)Elt)->Value;
		}
		else if (Elt->Context->Id == MATROSKA_ContextDateUTC.Id)
		{
			File->Seg.DateUTC = EBML_DateTime((ebml_date*)Elt);
		}
		else if (Elt->Context->Id == MATROSKA_ContextTitle.Id)
		{
			File->Seg.Title = ((InputStream*)File->IOStream->io)->memalloc(File->IOStream->io, (size_t)(Elt->DataSize+1));
			strcpy(File->Seg.Title,((ebml_string*)Elt)->Buffer);
		}
		else if (Elt->Context->Id == MATROSKA_ContextMuxingApp.Id)
		{
			File->Seg.MuxingApp = ((InputStream*)File->IOStream->io)->memalloc(File->IOStream->io, (size_t)(Elt->DataSize+1));
			strcpy(File->Seg.MuxingApp,((ebml_string*)Elt)->Buffer);
		}
		else if (Elt->Context->Id == MATROSKA_ContextWritingApp.Id)
		{
			File->Seg.WritingApp = ((InputStream*)File->IOStream->io)->memalloc(File->IOStream->io, (size_t)(Elt->DataSize+1));
			strcpy(File->Seg.WritingApp,((ebml_string*)Elt)->Buffer);
		}
		else if (Elt->Context->Id == MATROSKA_ContextSegmentUID.Id)
		{
			if (Elt->DataSize!=16)
			{
				snprintf(err_msg,err_msgSize,"SegmentUID size is not %d bytes",(int)Elt->DataSize);
				return 0;
			}
			memcpy(File->Seg.UID,EBML_BinaryGetData((ebml_binary*)Elt),sizeof(File->Seg.UID));
		}
		else if (Elt->Context->Id == MATROSKA_ContextPrevUID.Id)
		{
			if (Elt->DataSize!=16)
			{
				snprintf(err_msg,err_msgSize,"PrevUID size is not %d bytes",(int)Elt->DataSize);
				return 0;
			}
			memcpy(File->Seg.PrevUID,EBML_BinaryGetData((ebml_binary*)Elt),sizeof(File->Seg.PrevUID));
		}
		else if (Elt->Context->Id == MATROSKA_ContextNextUID.Id)
		{
			if (Elt->DataSize!=16)
			{
				snprintf(err_msg,err_msgSize,"NextUID size is not %d bytes",(int)Elt->DataSize);
				return 0;
			}
			memcpy(File->Seg.NextUID,EBML_BinaryGetData((ebml_binary*)Elt),sizeof(File->Seg.NextUID));
		}
		else if (Elt->Context->Id == MATROSKA_ContextSegmentFilename.Id)
		{
			File->Seg.Filename = ((InputStream*)File->IOStream->io)->memalloc(File->IOStream->io, (size_t)(Elt->DataSize+1));
			strcpy(File->Seg.Filename,((ebml_string*)Elt)->Buffer);
		}
		else if (Elt->Context->Id == MATROSKA_ContextPrevFilename.Id)
		{
			File->Seg.PrevFilename = ((InputStream*)File->IOStream->io)->memalloc(File->IOStream->io, (size_t)(Elt->DataSize+1));
			strcpy(File->Seg.Filename,((ebml_string*)Elt)->Buffer);
		}
		else if (Elt->Context->Id == MATROSKA_ContextNextFilename.Id)
		{
			File->Seg.NextFilename = ((InputStream*)File->IOStream->io)->memalloc(File->IOStream->io, (size_t)(Elt->DataSize+1));
			strcpy(File->Seg.Filename,((ebml_string*)Elt)->Buffer);
		}
	}
	if (duration > 0.0)
		File->Seg.Duration = (timecode_t)(duration * File->Seg.TimestampScale);

	return 1;
}

static void releaseTrackEntry(TrackInfo *track, InputStream *io)
{
	if (track->CodecID) io->memfree(io, track->CodecID);
	if (track->CodecPrivate) io->memfree(io, track->CodecPrivate);
	if (track->Name) io->memfree(io, track->Name);
}

static bool_t parseTrackEntry(ebml_element *Track, MatroskaFile *File, char *err_msg, size_t err_msgSize)
{
	TrackInfo track,*Tracks;
	ebml_element *Elt,*TElt;

	if (ARRAYCOUNT(File->Tracks,TrackInfo) >= MAX_TRACKS)
		return 0;

	memset(&track,0,sizeof(track));
	track.DefaultDuration = INVALID_TIMECODE_T;
	track.Enabled = MATROSKA_ContextFlagEnabled.DefaultValue;
	track.Default = MATROSKA_ContextFlagDefault.DefaultValue;
	track.Lacing = MATROSKA_ContextFlagLacing.DefaultValue;
	track.DecodeAll = MATROSKA_ContextCodecDecodeAll.DefaultValue;
	track.TimecodeScale = (float)MATROSKA_ContextTrackTimestampScale.DefaultValue;
	memcpy(track.Language, "eng", 4);

	for (Elt = EBML_MasterChildren(Track);Elt;Elt = EBML_MasterNext(Elt))
	{
		if (Elt->Context->Id == MATROSKA_ContextTrackNumber.Id)
			track.Number = (int)EBML_IntegerValue(Elt);
		else if (Elt->Context->Id == MATROSKA_ContextTrackNumber.Id)
			track.UID = EBML_IntegerValue(Elt);
		else if (Elt->Context->Id == MATROSKA_ContextTrackType.Id)
		{
			if (EBML_IntegerValue(Elt)==0 || EBML_IntegerValue(Elt)>254)
			{
				snprintf(err_msg,err_msgSize,"Invalid track type: %d",(int)EBML_IntegerValue(Elt));
				goto fail;
			}
			track.Type = (uint8_t)EBML_IntegerValue(Elt);
		}
		else if (Elt->Context->Id == MATROSKA_ContextFlagEnabled.Id)
			track.Enabled = EBML_IntegerValue(Elt)!=0;
		else if (Elt->Context->Id == MATROSKA_ContextFlagDefault.Id)
			track.Default = EBML_IntegerValue(Elt)!=0;
		else if (Elt->Context->Id == MATROSKA_ContextFlagLacing.Id)
			track.Lacing = EBML_IntegerValue(Elt)!=0;
		else if (Elt->Context->Id == MATROSKA_ContextCodecDecodeAll.Id)
			track.DecodeAll = EBML_IntegerValue(Elt)!=0;
		else if (Elt->Context->Id == MATROSKA_ContextMinCache.Id)
		{
			if (EBML_IntegerValue(Elt) > 0xFF)
			{
				snprintf(err_msg,err_msgSize,"MinCache is too large: %d",(int)EBML_IntegerValue(Elt));
				goto fail;
			}
			track.MinCache = (uint8_t)EBML_IntegerValue(Elt);
		}
		else if (Elt->Context->Id == MATROSKA_ContextMaxCache.Id)
		{
			if (EBML_IntegerValue(Elt) > 0x7FFFFFFF)
			{
				snprintf(err_msg,err_msgSize,"MaxCache is too large: %d",(int)EBML_IntegerValue(Elt));
				goto fail;
			}
			track.MaxCache = (size_t)EBML_IntegerValue(Elt);
		}
		else if (Elt->Context->Id == MATROSKA_ContextDefaultDuration.Id)
			track.DefaultDuration = EBML_IntegerValue(Elt);
		else if (Elt->Context->Id == MATROSKA_ContextTrackTimestampScale.Id)
			track.TimecodeScale = (float)((ebml_float*)Elt)->Value;
		else if (Elt->Context->Id == MATROSKA_ContextMaxBlockAdditionID.Id)
			track.MaxBlockAdditionID = (size_t)EBML_IntegerValue(Elt);
		else if (Elt->Context->Id == MATROSKA_ContextCodecDelay.Id) {
			track.CodecDelay = EBML_IntegerValue(Elt);
		}
		else if (Elt->Context->Id == MATROSKA_ContextLanguage.Id)
		{
			size_t copy = (Elt->DataSize>3) ? 3 : (size_t)Elt->DataSize;
			memcpy(track.Language,((ebml_string*)Elt)->Buffer,copy);
			memset(track.Language + copy,0,4-copy);
		}
		else if (Elt->Context->Id == MATROSKA_ContextCodecID.Id)
		{
			track.CodecID = ((InputStream*)File->IOStream->io)->memalloc(File->IOStream->io, (size_t)(Elt->DataSize+1));
			strcpy(track.CodecID,((ebml_string*)Elt)->Buffer);
		}
		else if (Elt->Context->Id == MATROSKA_ContextCodecPrivate.Id)
		{
			if (Elt->DataSize > 256*1024)
			{
				snprintf(err_msg,err_msgSize,"CodecPrivate is too large: %d",(int)EBML_IntegerValue(Elt));
				goto fail;
			}
			track.CodecPrivateSize = (size_t)(Elt->DataSize);
			track.CodecPrivate = ((InputStream*)File->IOStream->io)->memalloc(File->IOStream->io, track.CodecPrivateSize);
			memcpy(track.CodecPrivate,EBML_BinaryGetData((ebml_binary*)Elt),track.CodecPrivateSize);
		}
		else if (Elt->Context->Id == MATROSKA_ContextTrackOverlay.Id)
		{
			if (EBML_IntegerValue(Elt)==0 || EBML_IntegerValue(Elt)>254)
			{
				snprintf(err_msg,err_msgSize,"Track number in TrackOverlay is too large: %d",(int)EBML_IntegerValue(Elt));
				goto fail;
			}
			track.TrackOverlay = (uint8_t)EBML_IntegerValue(Elt);
		}
		else if (Elt->Context->Id == MATROSKA_ContextVideo.Id)
		{
			for (TElt = EBML_MasterChildren(Elt);TElt;TElt = EBML_MasterNext(TElt))
			{
				if (TElt->Context->Id == MATROSKA_ContextFlagInterlaced.Id)
					track.AV.Video.Interlaced = EBML_IntegerValue(TElt)!=0;
				else if (TElt->Context->Id == MATROSKA_ContextStereoMode.Id)
				{
					if (TElt->DataSize > 3)
					{
						snprintf(err_msg,err_msgSize,"Invalid stereo mode: %d",(int)EBML_IntegerValue(TElt));
						goto fail;
					}
					track.AV.Video.StereoMode = (uint8_t)EBML_IntegerValue(TElt);
				}
				else if (TElt->Context->Id == MATROSKA_ContextPixelWidth.Id)
				{
					if (EBML_IntegerValue(TElt) > 0xFFFFFFFF)
					{
						snprintf(err_msg,err_msgSize,"PixelWidth is too large: %d",(int)EBML_IntegerValue(TElt));
						goto fail;
					}
					track.AV.Video.PixelWidth = (uint32_t)EBML_IntegerValue(TElt);
					if (!track.AV.Video.DisplayWidth)
						track.AV.Video.DisplayWidth = track.AV.Video.PixelWidth;
				}
				else if (TElt->Context->Id == MATROSKA_ContextPixelHeight.Id)
				{
					if (EBML_IntegerValue(TElt) > 0xFFFFFFFF)
					{
						snprintf(err_msg,err_msgSize,"PixelHeight is too large: %d",(int)EBML_IntegerValue(TElt));
						goto fail;
					}
					track.AV.Video.PixelHeight = (uint32_t)EBML_IntegerValue(TElt);
					if (!track.AV.Video.DisplayHeight)
						track.AV.Video.DisplayHeight = track.AV.Video.PixelHeight;
				}
				else if (TElt->Context->Id == MATROSKA_ContextDisplayWidth.Id)
				{
					if (EBML_IntegerValue(TElt) > 0xFFFFFFFF)
					{
						snprintf(err_msg,err_msgSize,"DisplayWidth is too large: %d",(int)EBML_IntegerValue(TElt));
						goto fail;
					}
					track.AV.Video.DisplayWidth = (uint32_t)EBML_IntegerValue(TElt);
				}
				else if (TElt->Context->Id == MATROSKA_ContextDisplayHeight.Id)
				{
					if (EBML_IntegerValue(TElt) > 0xFFFFFFFF)
					{
						snprintf(err_msg,err_msgSize,"DisplayHeight is too large: %d",(int)EBML_IntegerValue(TElt));
						goto fail;
					}
					track.AV.Video.DisplayHeight = (uint32_t)EBML_IntegerValue(TElt);
				}
				else if (TElt->Context->Id == MATROSKA_ContextDisplayUnit.Id)
				{
					if (EBML_IntegerValue(TElt) > 2)
					{
						snprintf(err_msg,err_msgSize,"DisplayUnit is too large: %d",(int)EBML_IntegerValue(TElt));
						goto fail;
					}
					track.AV.Video.DisplayUnit = (uint8_t)EBML_IntegerValue(TElt);
				}
				else if (TElt->Context->Id == MATROSKA_ContextAspectRatioType.Id)
				{
					if (EBML_IntegerValue(TElt) > 2)
					{
						snprintf(err_msg,err_msgSize,"AspectRatioType is too large: %d",(int)EBML_IntegerValue(TElt));
						goto fail;
					}
					track.AV.Video.AspectRatioType = (uint8_t)EBML_IntegerValue(TElt);
				}
				else if (TElt->Context->Id == MATROSKA_ContextPixelCropBottom.Id)
				{
					if (EBML_IntegerValue(TElt) > 0xFFFFFFFF)
					{
						snprintf(err_msg,err_msgSize,"PixelCropBottom is too large: %d",(int)EBML_IntegerValue(TElt));
						goto fail;
					}
					track.AV.Video.CropB = (uint32_t)EBML_IntegerValue(TElt);
				}
				else if (TElt->Context->Id == MATROSKA_ContextPixelCropTop.Id)
				{
					if (EBML_IntegerValue(TElt) > 0xFFFFFFFF)
					{
						snprintf(err_msg,err_msgSize,"PixelCropTop is too large: %d",(int)EBML_IntegerValue(TElt));
						goto fail;
					}
					track.AV.Video.CropT = (uint32_t)EBML_IntegerValue(TElt);
				}
				else if (TElt->Context->Id == MATROSKA_ContextPixelCropLeft.Id)
				{
					if (EBML_IntegerValue(TElt) > 0xFFFFFFFF)
					{
						snprintf(err_msg,err_msgSize,"PixelCropLeft is too large: %d",(int)EBML_IntegerValue(TElt));
						goto fail;
					}
					track.AV.Video.CropL = (uint32_t)EBML_IntegerValue(TElt);
				}
				else if (TElt->Context->Id == MATROSKA_ContextPixelCropRight.Id)
				{
					if (EBML_IntegerValue(TElt) > 0xFFFFFFFF)
					{
						snprintf(err_msg,err_msgSize,"PixelCropRight is too large: %d",(int)EBML_IntegerValue(TElt));
						goto fail;
					}
					track.AV.Video.CropR = (uint32_t)EBML_IntegerValue(TElt);
				}
				else if (TElt->Context->Id == MATROSKA_ContextColourSpace.Id)
				{
					if (TElt->DataSize != 4)
					{
						snprintf(err_msg,err_msgSize,"ColourSpace is too large: %d",TElt->DataSize);
						goto fail;
					}
					track.AV.Video.ColourSpace = LOAD32LE(EBML_BinaryGetData((ebml_binary*)TElt));
				}
				else if (TElt->Context->Id == MATROSKA_ContextGammaValue.Id) {
					track.AV.Video.GammaValue = (float)((ebml_float*)TElt)->Value;
				}
				else if (TElt->Context->Id == MATROSKA_ContextColour.Id) {
					for (ebml_element* FElt = EBML_MasterChildren(TElt); FElt; FElt = EBML_MasterNext(FElt)) {
						if (FElt->Context->Id == MATROSKA_ContextMatrixCoefficients.Id) {
							track.AV.Video.Colour.MatrixCoefficient = EBML_IntegerValue(FElt);
							if (track.AV.Video.Colour.MatrixCoefficient > 14)
							{
								snprintf(err_msg, err_msgSize, "MatrixCoefficient is unkown: %d", TElt->DataSize);
								track.AV.Video.Colour.MatrixCoefficient = 0;
							}
						}
						else if (FElt->Context->Id == MATROSKA_ContextBitsPerChannel.Id) {
							track.AV.Video.Colour.BitsPerChannel = EBML_IntegerValue(FElt);
						}
						else if (FElt->Context->Id == MATROSKA_ContextChromaSubsamplingHorz.Id) {
							track.AV.Video.Colour.ChromaSubsamplingHorz = EBML_IntegerValue(FElt);
						}
						else if (FElt->Context->Id == MATROSKA_ContextChromaSubsamplingVert.Id) {
							track.AV.Video.Colour.ChromaSubsamplingVert = EBML_IntegerValue(FElt);
						}
						else if (FElt->Context->Id == MATROSKA_ContextCbSubsamplingHorz.Id) {
							track.AV.Video.Colour.CbSubsamplingHorz = EBML_IntegerValue(FElt);
						}
						else if (FElt->Context->Id == MATROSKA_ContextCbSubsamplingVert.Id) {
							track.AV.Video.Colour.CbSubsamplingVert = EBML_IntegerValue(FElt);
						}
						else if (FElt->Context->Id == MATROSKA_ContextChromaSitingHorz.Id) {
							track.AV.Video.Colour.ChromaSitingHorz = EBML_IntegerValue(FElt);
						}
						else if (FElt->Context->Id == MATROSKA_ContextChromaSitingVert.Id) {
							track.AV.Video.Colour.ChromaSitingVert = EBML_IntegerValue(FElt);
						}
						else if (FElt->Context->Id == MATROSKA_ContextRange.Id) {
							track.AV.Video.Colour.Range = EBML_IntegerValue(FElt);
						}
						else if (FElt->Context->Id == MATROSKA_ContextTransferCharacteristics.Id) {
							track.AV.Video.Colour.TransferCharacteristics = EBML_IntegerValue(FElt);
						}
						else if (FElt->Context->Id == MATROSKA_ContextPrimaries.Id) {
							track.AV.Video.Colour.Primaries = EBML_IntegerValue(FElt);
						}
						else if (FElt->Context->Id == MATROSKA_ContextMaxCLL.Id) {
							track.AV.Video.Colour.MaxCLL = EBML_IntegerValue(FElt);
						}
						else if (FElt->Context->Id == MATROSKA_ContextMaxFALL.Id) {
							track.AV.Video.Colour.MaxFALL = EBML_IntegerValue(FElt);
						}
					}
				}
			}
		}
		else if (Elt->Context->Id == MATROSKA_ContextAudio.Id)
		{
			track.AV.Audio.Channels = (uint8_t)MATROSKA_ContextChannels.DefaultValue;
			track.AV.Audio.SamplingFreq = (float)MATROSKA_ContextSamplingFrequency.DefaultValue;

			for (TElt = EBML_MasterChildren(Elt);TElt;TElt = EBML_MasterNext(TElt))
			{
				if (TElt->Context->Id == MATROSKA_ContextSamplingFrequency.Id)
				{
					track.AV.Audio.SamplingFreq = (float)((ebml_float*)TElt)->Value;
				}
				else if (TElt->Context->Id == MATROSKA_ContextOutputSamplingFrequency.Id)
					track.AV.Audio.OutputSamplingFreq = (float)((ebml_float*)TElt)->Value;
				else if (TElt->Context->Id == MATROSKA_ContextChannels.Id)
				{
					if (EBML_IntegerValue(TElt)==0 || EBML_IntegerValue(TElt)>0xFF)
					{
						snprintf(err_msg,err_msgSize,"Invalid Channels value: %d",(int)EBML_IntegerValue(TElt));
						goto fail;
					}
					track.AV.Audio.Channels = (uint8_t)EBML_IntegerValue(TElt);
				}
				else if (TElt->Context->Id == MATROSKA_ContextBitsPerChannel.Id)
				{
					if (EBML_IntegerValue(TElt)==0 || EBML_IntegerValue(TElt)>0xFF)
					{
						snprintf(err_msg,err_msgSize,"Invalid BitDepth value: %d",(int)EBML_IntegerValue(TElt));
						goto fail;
					}
					track.AV.Audio.BitDepth = (uint8_t)EBML_IntegerValue(TElt);
				}
			}
			if (!track.AV.Audio.OutputSamplingFreq)
				track.AV.Audio.OutputSamplingFreq = track.AV.Audio.SamplingFreq;
		}
		// TODO: ContentEncoding
	}

	if (!track.CodecID)
	{
		strncpy(err_msg,"Track has no Codec ID",err_msgSize);
		goto fail;
	}

	// check for duplicate track UID entries
	if (track.UID)
	{
		for (Tracks=ARRAYBEGIN(File->Tracks,TrackInfo);Tracks!=ARRAYEND(File->Tracks,TrackInfo);++Tracks)
		{
			if (Tracks->UID == track.UID)
			{
				snprintf(err_msg,err_msgSize,"A track with UID 0x" PRIx64 " already exists",track.UID);
				goto fail;
			}
		}
	}

	ArrayAppend(&File->Tracks,&track,sizeof(track),256);
	return 1;

fail:
	releaseTrackEntry(&track, File->IOStream->io);
	return 0;
}

static bool_t parseTracks(ebml_element *Tracks, MatroskaFile *File, char *err_msg, size_t err_msgSize)
{
	ebml_parser_context RContext;
	ebml_element *Elt;

	RContext.Context = Tracks->Context;
	if (EBML_ElementIsFiniteSize(Tracks))
		RContext.EndPosition = EBML_ElementPositionEnd(Tracks);
	else
		RContext.EndPosition = INVALID_FILEPOS_T;
	RContext.UpContext = &File->L1Context;
	if (EBML_ElementReadData(Tracks,(stream*)File->IOStream,&RContext,1,SCOPE_ALL_DATA,0)!=ERR_NONE)
	{
		strncpy(err_msg,"Failed to read the Tracks",err_msgSize);
		File->pTracks = INVALID_FILEPOS_T;
		return 0;
	}
	File->pTracks = Tracks->ElementPosition;

	for (Elt = EBML_MasterChildren(Tracks);Elt;Elt = EBML_MasterNext(Elt))
	{
		if (Elt->Context->Id == MATROSKA_ContextTrackEntry.Id)
			if (!parseTrackEntry(Elt, File, err_msg, err_msgSize))
				break;
	}
	File->TrackList = Tracks;

	return 1;
}

static bool_t parseCues(ebml_element *Cues, MatroskaFile *File, char *err_msg, size_t err_msgSize)
{
	ebml_parser_context RContext;

	RContext.Context = Cues->Context;
	if (EBML_ElementIsFiniteSize(Cues))
		RContext.EndPosition = EBML_ElementPositionEnd(Cues);
	else
		RContext.EndPosition = INVALID_FILEPOS_T;
	RContext.UpContext = &File->L1Context;
	if (EBML_ElementReadData(Cues,(stream*)File->IOStream,&RContext,1,SCOPE_ALL_DATA, 0)!=ERR_NONE)
	{
		strncpy(err_msg,"Failed to read the Cues",err_msgSize);
		File->pCues = INVALID_FILEPOS_T;
		return 0;
	}
	File->pCues = Cues->ElementPosition;
	File->CueList = Cues;

	return 1;
}

static bool_t parseAttachments(ebml_element *Attachments, MatroskaFile *File, char *err_msg, size_t err_msgSize)
{
	ebml_parser_context RContext;
	ebml_element *Elt, *Elt2;
	size_t Count;
	Attachment *At;

	RContext.Context = Attachments->Context;
	if (EBML_ElementIsFiniteSize(Attachments))
		RContext.EndPosition = EBML_ElementPositionEnd(Attachments);
	else
		RContext.EndPosition = INVALID_FILEPOS_T;
	RContext.UpContext = &File->L1Context;
	if (EBML_ElementReadData(Attachments,(stream*)File->IOStream,&RContext,1,SCOPE_PARTIAL_DATA, 0)!=ERR_NONE)
	{
		strncpy(err_msg,"Failed to read the Attachments",err_msgSize);
		File->pAttachments = INVALID_FILEPOS_T;
		return 0;
	}
	File->pAttachments = Attachments->ElementPosition;

	Count=0;
	Elt = EBML_MasterFindFirstElt(Attachments, &MATROSKA_ContextAttachedFile, 0,0);
	while (Elt)
	{
		++Count;
		Elt = EBML_MasterFindNextElt(Attachments, Elt, 0,0);
	}
	ArrayResize(&File->Attachments,Count*sizeof(Attachment),0);
	ArrayZero(&File->Attachments);

	for (Elt = EBML_MasterFindFirstElt(Attachments, &MATROSKA_ContextAttachedFile, 0,0),At=ARRAYBEGIN(File->Attachments,Attachment);
		At!=ARRAYEND(File->Attachments,Attachment); ++At, Elt = EBML_MasterFindNextElt(Attachments, Elt, 0,0))
	{
		At->Length = INVALID_FILEPOS_T;
		At->Position = INVALID_FILEPOS_T;
		for (Elt2=EBML_MasterChildren(Elt);Elt2;Elt2=EBML_MasterNext(Elt2))
		{
			if (Elt2->Context->Id == MATROSKA_ContextFileName.Id)
			{
				At->Name = ((InputStream*)File->IOStream->io)->memalloc(File->IOStream->io, (size_t)(Elt2->DataSize+1));
				strcpy(At->Name,((ebml_string*)Elt2)->Buffer);
			}
			else if (Elt2->Context->Id == MATROSKA_ContextFileData.Id)
			{
				At->Position = EBML_ElementPositionData(Elt2);
				At->Length = Elt2->DataSize;
			}
			else if (Elt2->Context->Id == MATROSKA_ContextFileUID.Id)
				At->UID = EBML_IntegerValue(Elt2);
			else if (Elt2->Context->Id == MATROSKA_ContextFileMimeType.Id)
			{
				At->MimeType = ((InputStream*)File->IOStream->io)->memalloc(File->IOStream->io, (size_t)(Elt2->DataSize+1));
				strcpy(At->MimeType,((ebml_string*)Elt2)->Buffer);
			}
			else if (Elt2->Context->Id == MATROSKA_ContextFileDescription.Id)
			{
				At->Description = ((InputStream*)File->IOStream->io)->memalloc(File->IOStream->io, (size_t)(Elt2->DataSize+1));
				strcpy(At->Description,((ebml_string*)Elt2)->Buffer);
			}
		}
	}

	return 1;
}

static bool_t addChapterDisplay(ebml_element *ChapterDisplay, MatroskaFile *File, struct Chapter *Chapter)
{
	struct ChapterDisplay *pDisplay,Display;
	ebml_element *Elt;

	if (!ArrayAppend(&Chapter->aDisplays,&Display,sizeof(struct ChapterDisplay),512))
		return 0;
	pDisplay = ARRAYEND(Chapter->aDisplays,struct ChapterDisplay)-1;
	memset(pDisplay,0,sizeof(*pDisplay));
	memcpy(pDisplay->Language, (char*)MATROSKA_ContextChapLanguage.DefaultValue, 4);

	for (Elt=EBML_MasterChildren(ChapterDisplay); Elt; Elt=EBML_MasterNext(Elt))
	{
		if (Elt->Context->Id == MATROSKA_ContextChapString.Id)
		{
			pDisplay->String = ((InputStream*)File->IOStream->io)->memalloc(File->IOStream->io, (size_t)(Elt->DataSize+1));
			strcpy(pDisplay->String,((ebml_string*)Elt)->Buffer);
		}
		else if (Elt->Context->Id == MATROSKA_ContextChapLanguage.Id)
		{
			size_t copy = (Elt->DataSize>3) ? 3 : (size_t)Elt->DataSize;
			memcpy(pDisplay->Language,((ebml_string*)Elt)->Buffer,copy);
			memset(pDisplay->Language + copy,0,4-copy);
		}
		else if (Elt->Context->Id == MATROSKA_ContextChapCountry.Id)
		{
			size_t copy = (Elt->DataSize>3) ? 3 : (size_t)Elt->DataSize;
			memcpy(pDisplay->Country,((ebml_string*)Elt)->Buffer,copy);
			memset(pDisplay->Country+ copy,0,4-copy);
		}
	}

	return 1;
}

static bool_t parseChapter(ebml_element *Chapter, MatroskaFile *File, char *err_msg, size_t err_msgSize, struct Chapter *Parent, array *Editions)
{
	struct Chapter *pChapter,Chap;
	ebml_element *Elt;

	pChapter = &Chap;
	for (Elt=EBML_MasterChildren(Chapter); Elt; Elt=EBML_MasterNext(Elt))
	{
		if (Elt->Context->Id == MATROSKA_ContextChapterAtom.Id)
		{
			if (!ArrayAppend(&Parent->aChildren,&Chap,sizeof(Chap),512))
				return 0;
			pChapter = ARRAYEND(Parent->aChildren,struct Chapter)-1;
			memset(pChapter,0,sizeof(*pChapter));
			pChapter->Start = INVALID_TIMECODE_T;
			if (!parseChapter(Elt,File,err_msg,err_msgSize,pChapter,NULL))
				ArrayRemove(&Parent->aChildren,struct Chapter,pChapter,NULL,NULL);
		}
		// Atom
		else if (Elt->Context->Id == MATROSKA_ContextChapterUID.Id)
			Parent->UID = EBML_IntegerValue(Elt);
		else if (Elt->Context->Id == MATROSKA_ContextChapterTimeStart.Id)
			Parent->Start = EBML_IntegerValue(Elt);
		else if (Elt->Context->Id == MATROSKA_ContextChapterTimeEnd.Id)
			Parent->End = EBML_IntegerValue(Elt);
		else if (Elt->Context->Id == MATROSKA_ContextChapterFlagHidden.Id)
			Parent->Hidden = EBML_IntegerValue(Elt)!=0;
		else if (Elt->Context->Id == MATROSKA_ContextChapterFlagEnabled.Id)
			Parent->Enabled = EBML_IntegerValue(Elt)!=0;
		else if (Elt->Context->Id == MATROSKA_ContextChapterDisplay.Id)
			addChapterDisplay(Elt, File, Parent);
		// Edition
		else if (Elt->Context->Id == MATROSKA_ContextEditionUID.Id)
			Parent->UID = EBML_IntegerValue(Elt);
		else if (Elt->Context->Id == MATROSKA_ContextEditionFlagHidden.Id)
			Parent->Hidden = EBML_IntegerValue(Elt)!=0;
		else if (Elt->Context->Id == MATROSKA_ContextEditionFlagDefault.Id)
			Parent->Default = EBML_IntegerValue(Elt)!=0;
		else if (Elt->Context->Id == MATROSKA_ContextEditionFlagOrdered.Id)
			Parent->Ordered = EBML_IntegerValue(Elt)!=0;
	}

	Parent->nChildren = ARRAYCOUNT(Parent->aChildren,struct Chapter);
	Parent->Children = ARRAYBEGIN(Parent->aChildren,struct Chapter);
	Parent->nDisplay = ARRAYCOUNT(Parent->aDisplays,struct ChapterDisplay);
	Parent->Display = ARRAYBEGIN(Parent->aDisplays,struct ChapterDisplay);

	return 1;
}

static bool_t parseChapters(ebml_element *Chapters, MatroskaFile *File, char *err_msg, size_t err_msgSize)
{
	ebml_parser_context RContext;
	struct Chapter *pChapter,Chap;
	ebml_element *Elt;

	RContext.Context = Chapters->Context;
	if (EBML_ElementIsFiniteSize(Chapters))
		RContext.EndPosition = EBML_ElementPositionEnd(Chapters);
	else
		RContext.EndPosition = INVALID_FILEPOS_T;
	RContext.UpContext = &File->L1Context;
	if (EBML_ElementReadData(Chapters,(stream*)File->IOStream,&RContext,1,SCOPE_ALL_DATA,0)!=ERR_NONE)
	{
		strncpy(err_msg,"Failed to read the Chapters",err_msgSize);
		File->pChapters = INVALID_FILEPOS_T;
		return 0;
	}
	File->pChapters = Chapters->ElementPosition;

	pChapter = &Chap;
	memset(pChapter,0,sizeof(*pChapter));
	Chap.Start = INVALID_TIMECODE_T;
	Chap.Ordered = MATROSKA_ContextEditionFlagOrdered.DefaultValue;
	Chap.Hidden = MATROSKA_ContextEditionFlagHidden.DefaultValue;
	Chap.Default = MATROSKA_ContextEditionFlagDefault.DefaultValue;
	for (Elt=EBML_MasterChildren(Chapters); Elt; Elt=EBML_MasterNext(Elt))
	{
		if (Elt->Context->Id == MATROSKA_ContextChapterAtom.Id)
		{
			if (!ArrayAppend(&File->Chapters,&Chap,sizeof(Chap),512))
				return 0;
			pChapter = ARRAYEND(File->Chapters,struct Chapter)-1;
			if (!parseChapter(Elt, File, err_msg, err_msgSize, pChapter, &File->Chapters))
				ArrayRemove(&File->Chapters,struct Chapter,pChapter,NULL,NULL);
		}
	}

	return 1;
}

static bool_t parseTargets(ebml_element *Targets, MatroskaFile *File, char *err_msg, size_t err_msgSize, struct Tag *Parent)
{
	ebml_element *Elt;
	uint8_t Level;
	struct Target *pTarget,Target;

	Elt = EBML_MasterFindFirstElt(Targets, &MATROSKA_ContextTargetTypeValue, 1, 1);
	if (!Elt || EBML_IntegerValue(Elt) > 0xFF)
		return 0;

	Level = (uint8_t)EBML_IntegerValue(Elt);

	pTarget = &Target;
	memset(pTarget,0,sizeof(*pTarget));
	for (Elt=EBML_MasterChildren(Targets); Elt; Elt=EBML_MasterNext(Elt))
	{
		if (Elt->Context->Id == MATROSKA_ContextTagTrackUID.Id)
		{
			if (!ArrayAppend(&Parent->aTargets,&Target,sizeof(Target),512))
				return 0;
			pTarget = ARRAYEND(Parent->aTargets,struct Target)-1;
			pTarget->Type = TARGET_TRACK;
			pTarget->UID = EBML_IntegerValue(Elt);
			pTarget->Level = Level;
		}
		else if (Elt->Context->Id == MATROSKA_ContextTagChapterUID.Id)
		{
			if (!ArrayAppend(&Parent->aTargets,&Target,sizeof(Target),512))
				return 0;
			pTarget = ARRAYEND(Parent->aTargets,struct Target)-1;
			pTarget->Type = TARGET_CHAPTER;
			pTarget->UID = EBML_IntegerValue(Elt);
			pTarget->Level = Level;
		}
		else if (Elt->Context->Id == MATROSKA_ContextTagAttachmentUID.Id)
		{
			if (!ArrayAppend(&Parent->aTargets,&Target,sizeof(Target),512))
				return 0;
			pTarget = ARRAYEND(Parent->aTargets,struct Target)-1;
			pTarget->Type = TARGET_ATTACHMENT;
			pTarget->UID = EBML_IntegerValue(Elt);
			pTarget->Level = Level;
		}
		else if (Elt->Context->Id == MATROSKA_ContextTagEditionUID.Id)
		{
			if (!ArrayAppend(&Parent->aTargets,&Target,sizeof(Target),512))
				return 0;
			pTarget = ARRAYEND(Parent->aTargets,struct Target)-1;
			pTarget->Type = TARGET_EDITION;
			pTarget->UID = EBML_IntegerValue(Elt);
			pTarget->Level = Level;
		}
	}

	return 1;
}

static bool_t parseSimpleTag(ebml_element *SimpleTag, MatroskaFile *File, char *err_msg, size_t err_msgSize, struct Tag *Parent)
{
	ebml_element *Elt;
	struct SimpleTag simpleTag;

	memset(&simpleTag,0,sizeof(simpleTag));
	simpleTag.Default = MATROSKA_ContextTagDefault.DefaultValue;
	memcpy(simpleTag.Language, (const char*)MATROSKA_ContextTagLanguage.DefaultValue, 4);
	for (Elt=EBML_MasterChildren(SimpleTag); Elt; Elt=EBML_MasterNext(Elt))
	{
		if (Elt->Context->Id == MATROSKA_ContextTagName.Id)
		{
			simpleTag.Name = ((InputStream*)File->IOStream->io)->memalloc(File->IOStream->io, (size_t)(Elt->DataSize+1));
			strcpy(simpleTag.Name,((ebml_string*)Elt)->Buffer);
		}
		else if (Elt->Context->Id == MATROSKA_ContextTagString.Id)
		{
			simpleTag.Value = ((InputStream*)File->IOStream->io)->memalloc(File->IOStream->io, (size_t)(Elt->DataSize+1));
			strcpy(simpleTag.Value,((ebml_string*)Elt)->Buffer);
		}
		else if (Elt->Context->Id == MATROSKA_ContextTagLanguage.Id)
		{
			size_t copy = (Elt->DataSize>3) ? 3 : (size_t)Elt->DataSize;
			memcpy(simpleTag.Language,((ebml_string*)Elt)->Buffer,copy);
			memset(simpleTag.Language + copy,0,4-copy);
		}
		else if (Elt->Context->Id == MATROSKA_ContextTagString.Id)
			simpleTag.Default = EBML_IntegerValue(Elt)!=0;
	}
	
	if (!simpleTag.Value || !simpleTag.Name || !ArrayAppend(&Parent->aSimpleTags,&simpleTag,sizeof(simpleTag),256))
	{
		if (simpleTag.Value) ((InputStream*)File->IOStream->io)->memfree(File->IOStream->io, simpleTag.Value);
		if (simpleTag.Name) ((InputStream*)File->IOStream->io)->memfree(File->IOStream->io, simpleTag.Name);
		return 0;
	}

	Elt = EBML_MasterFindFirstElt(SimpleTag, &MATROSKA_ContextSimpleTag, 0, 0);
	while (Elt)
	{
		parseSimpleTag(Elt, File, err_msg, err_msgSize, Parent);
		Elt = EBML_MasterFindNextElt(SimpleTag, Elt, 0, 0);
	}

	return 1;
}

static bool_t parseTag(ebml_element *Tag, MatroskaFile *File, char *err_msg, size_t err_msgSize, struct Tag *Parent)
{
	ebml_element *Elt;

	for (Elt=EBML_MasterChildren(Tag); Elt; Elt=EBML_MasterNext(Elt))
	{
		if (Elt->Context->Id == MATROSKA_ContextTargets.Id)
			parseTargets(Elt,File,err_msg,err_msgSize,Parent);
		else if (Elt->Context->Id == MATROSKA_ContextSimpleTag.Id)
			parseSimpleTag(Elt,File,err_msg,err_msgSize,Parent);
	}

	Parent->Targets = ARRAYBEGIN(Parent->aTargets,struct Target);
	Parent->nTargets = ARRAYCOUNT(Parent->aTargets,struct Target);
	Parent->SimpleTags = ARRAYBEGIN(Parent->aSimpleTags,struct SimpleTag);
	Parent->nSimpleTags = ARRAYCOUNT(Parent->aSimpleTags,struct SimpleTag);

	return 1;
}

static bool_t parseTags(ebml_element *Tags, MatroskaFile *File, char *err_msg, size_t err_msgSize)
{
	ebml_parser_context RContext;
	struct Tag *pTag,Tag;
	ebml_element *Elt;

	RContext.Context = Tags->Context;
	if (EBML_ElementIsFiniteSize(Tags))
		RContext.EndPosition = EBML_ElementPositionEnd(Tags);
	else
		RContext.EndPosition = INVALID_FILEPOS_T;
	RContext.UpContext = &File->L1Context;
	if (EBML_ElementReadData(Tags,(stream*)File->IOStream,&RContext,1,SCOPE_ALL_DATA,0)!=ERR_NONE)
	{
		strncpy(err_msg,"Failed to read the Tags",err_msgSize);
		File->pTags = INVALID_FILEPOS_T;
		return 0;
	}
	File->pTags = Tags->ElementPosition;

	pTag = &Tag;
	memset(pTag,0,sizeof(*pTag));
	for (Elt=EBML_MasterChildren(Tags); Elt; Elt=EBML_MasterNext(Elt))
	{
		if (Elt->Context->Id == MATROSKA_ContextTag.Id)
		{
			if (!ArrayAppend(&File->Tags,&Tag,sizeof(Tag),512))
				return 0;
			pTag = ARRAYEND(File->Tags,struct Tag)-1;
			if (!parseTag(Elt, File, err_msg, err_msgSize, pTag))
				ArrayRemove(&File->Tags,struct Tag,pTag,NULL,NULL);
		}
	}


	return 1;
}

MatroskaFile *mkv_OpenInput(InputStream *io, char *err_msg, size_t err_msgSize)
{
	int UpperLevel;
	ebml_element *Head, *Elt;

	MatroskaFile *File = io->memalloc(io,sizeof(*File));
	if (!File)
	{
		strncpy(err_msg,"Out of memory",err_msgSize);
		return NULL;
	}

	memset(File,0,sizeof(*File));
	File->pSegmentInfo = INVALID_FILEPOS_T;
	File->pTracks = INVALID_FILEPOS_T;
	File->pCues = INVALID_FILEPOS_T;
	File->pAttachments = INVALID_FILEPOS_T;
	File->pChapters = INVALID_FILEPOS_T;
	File->pTags = INVALID_FILEPOS_T;
	File->pFirstCluster = INVALID_FILEPOS_T;
	File->flags = 0;

	io->progress(io,0,0);
	io->ioseek(io,0,SEEK_SET);

	// find a segment
	File->IOStream = (haali_stream*)NodeCreate(io->AnyNode,HAALI_STREAM_CLASS);
	if (!File->IOStream)
	{
		mkv_CloseInput(File);
		strncpy(err_msg,"Out of memory",err_msgSize);
		return NULL;
	}
	File->IOStream->io = io;

	File->L0Context.Context = &MATROSKA_ContextStream;
	File->L0Context.EndPosition = ((InputStream*)File->IOStream->io)->getfilesize(File->IOStream->io);
	File->L0Context.UpContext = NULL;
	UpperLevel = 0;
	Head = EBML_FindNextElement((stream*)File->IOStream,&File->L0Context,&UpperLevel,0);
	if (!Head)
	{
		mkv_CloseInput(File);
		strncpy(err_msg,"Out of memory",err_msgSize);
		return NULL;
	}

	if (Head->Context->Id == MATROSKA_ContextSegment.Id)
	{
		mkv_CloseInput(File);
		strncpy(err_msg,"First element in file is not EBML",err_msgSize);
		return NULL;
	}

	if (EBML_ElementReadData(Head,(stream*)File->IOStream,&File->L0Context,1,SCOPE_ALL_DATA,0)!=ERR_NONE)
	{
		mkv_CloseInput(File);
		strncpy(err_msg,"Failed to read the EBML head",err_msgSize);
		NodeDelete((node*)Head);
		return NULL;
	}
	if (!CheckMatroskaHead(File,Head,&UpperLevel,err_msg,err_msgSize))
	{
		mkv_CloseInput(File);
		NodeDelete((node*)Head);
		return NULL;
	}
	NodeDelete((node*)Head);

	if (File->Profile == PROFILE_MATROSKA_V1 && File->DocVersion == 2)
		File->Profile = PROFILE_MATROSKA_V2;
	else if (File->Profile == PROFILE_MATROSKA_V1 && File->DocVersion == 3)
		File->Profile = PROFILE_MATROSKA_V3;
	else if (File->Profile == PROFILE_MATROSKA_V1 && File->DocVersion == 4)
		File->Profile = PROFILE_MATROSKA_V4;

//	File->Segment = EBML_FindNextElement((stream*)File->IOStream,&File->L0Context,&UpperLevel,0);
	if (!File->Segment)
	{
		mkv_CloseInput(File);
		strncpy(err_msg,"No segments found in the file",err_msgSize);
		return NULL;
	}

	// we want to read data until we find a seekhead or a trackinfo
	File->L1Context.Context = File->Segment->Context;
	if (EBML_ElementIsFiniteSize(File->Segment))
		File->L1Context.EndPosition = EBML_ElementPositionEnd(File->Segment);
	else {
		File->L1Context.EndPosition = INVALID_FILEPOS_T;
		//File->flags |= MKVF_AVOID_SEEKS;
	}
	File->L1Context.UpContext = &File->L0Context;
	UpperLevel = 0;
	Head = EBML_FindNextElement((stream*)File->IOStream,&File->L1Context,&UpperLevel,0);
	bool_t found_seek_head = 0;
	size_t index = 0;
	size_t cue_index = -1Ui64;
	int err = 1;
	array track_nodes;
	ArrayInit(&track_nodes);
	while (Head && (!EBML_ElementIsFiniteSize(File->Segment) || EBML_ElementPositionEnd(File->Segment) >= EBML_ElementPositionEnd(Head)))
	{
		if (Head->Context->Id == MATROSKA_ContextSeekHead.Id)
		{	
			found_seek_head = 1;
			err = parseSeekHead(Head, File, err_msg, err_msgSize);
			NodeDelete((node*)Head);
			if(!err){
				mkv_CloseInput(File);
				return NULL;
			}
		}
		else if (Head->Context->Id == MATROSKA_ContextInfo.Id) {
			err = parseSegmentInfo(Head, File, err_msg, err_msgSize);
			if (!err) {
				mkv_CloseInput(File);
				return NULL;
			}
		}
		else if (Head->Context->Id == MATROSKA_ContextTracks.Id) {
			err = parseTracks(Head, File, err_msg, err_msgSize);
//			ArrayAddEx(&track_nodes,1,sizeof(node*),&Head,NULL,NULL,1);
//			NodeDelete((node*)Head);
			if (!err) {
				mkv_CloseInput(File);
				return NULL;
			}
		}
		else if (Head->Context->Id == MATROSKA_ContextCues.Id) {
			if(cue_index == -1Ui64)
				cue_index = index;
			err = parseCues(Head, File, err_msg, err_msgSize);
			if (!err) {
				mkv_CloseInput(File);
				return NULL;
			}
		}
		else if (Head->Context->Id == MATROSKA_ContextAttachments.Id)
		{
			err = parseAttachments(Head, File, err_msg, err_msgSize);
			NodeDelete((node*)Head);
			if (!err) {
				mkv_CloseInput(File);
				return NULL;
			}
		}
		else if (Head->Context->Id == MATROSKA_ContextChapters.Id)
		{
			err = parseChapters(Head, File, err_msg, err_msgSize);
			NodeDelete((node*)Head);
			if (!err) {
				mkv_CloseInput(File);
				return NULL;
			}
		}
		else if (Head->Context->Id == MATROSKA_ContextTags.Id)
		{
			err = parseTags(Head, File, err_msg, err_msgSize);
			NodeDelete((node*)Head);
			if (!err) {
				mkv_CloseInput(File);
				return NULL;
			}
		}
		else
		{
			if (Head->Context->Id == MATROSKA_ContextCluster.Id && File->pFirstCluster == INVALID_FILEPOS_T) {
				//Streaming source detected
				if (cue_index + 1 == index && !found_seek_head) {
					File->flags |= MKVF_AVOID_SEEKS;
					//TODO: Parse tags in ReadFrame
					break;
				}
				File->pFirstCluster = Head->ElementPosition;
			}
			Elt = EBML_ElementSkipData(Head,(stream*)File->IOStream,&File->L1Context,NULL,1);
			NodeDelete((node*)Head);
			if (Elt)
			{
				Head = Elt;
				continue;
			}
		}
		Head = EBML_FindNextElement((stream*)File->IOStream,&File->L1Context,&UpperLevel,0);
		++index;
	}
	if(Head)
		NodeDelete((node*)Head);
	if (File->CueList && File->Segment_Info)
	{
		for (Elt = EBML_MasterChildren(File->CueList);Elt;Elt = EBML_MasterNext(Elt))
		{
			if (Elt->Context->Id == MATROSKA_ContextCuePoint.Id)
				MATROSKA_LinkCueSegmentInfo((matroska_cuepoint*)Elt, File->Segment_Info);
		}

		MATROSKA_CuesSort(File->CueList);
	}
//	for (int i = 0; i < ArraySize(&track_nodes); ++i) {
//		NodeDelete(*(ARRAYBEGIN(track_nodes,node*)+i));
//	}
//	ArrayClear(&track_nodes);
	return File;
}

static void releaseAttachments(array *Attachments, MatroskaFile *File)
{
	Attachment *At;
	for (At=ARRAYBEGIN(File->Attachments,Attachment); At!=ARRAYEND(File->Attachments,Attachment); ++At)
	{
		if (At->Name) ((InputStream*)File->IOStream->io)->memfree(File->IOStream->io, At->Name);
		if (At->MimeType) ((InputStream*)File->IOStream->io)->memfree(File->IOStream->io, At->MimeType);
		if (At->Description) ((InputStream*)File->IOStream->io)->memfree(File->IOStream->io, At->Description);
	}
	ArrayClear(Attachments);
}

static void releaseChapterDisplays(array *ChapterDisplays, MatroskaFile *File)
{
	struct ChapterDisplay *pChapter;

	for (pChapter = ARRAYBEGIN(*ChapterDisplays,struct ChapterDisplay); pChapter != ARRAYEND(*ChapterDisplays,struct ChapterDisplay); ++pChapter)
		((InputStream*)File->IOStream->io)->memfree(File->IOStream->io, pChapter->String);
	ArrayClear(ChapterDisplays);
}

static void releaseChapters(array *Chapters, MatroskaFile *File)
{
	struct Chapter *pChapter;

	for (pChapter = ARRAYBEGIN(*Chapters,struct Chapter); pChapter != ARRAYEND(*Chapters,struct Chapter); ++pChapter)
	{
		releaseChapterDisplays(&pChapter->aDisplays, File);
		releaseChapters(&pChapter->aChildren,File);
	}

	ArrayClear(Chapters);
}

static void releaseSimpleTags(array *SimpleTags, MatroskaFile *File)
{
	struct SimpleTag *pSimpleTag;
	for (pSimpleTag = ARRAYBEGIN(*SimpleTags,struct SimpleTag); pSimpleTag != ARRAYEND(*SimpleTags,struct SimpleTag); ++pSimpleTag)
	{
		if (pSimpleTag->Name) ((InputStream*)File->IOStream->io)->memfree(File->IOStream->io, pSimpleTag->Name);
		if (pSimpleTag->Value) ((InputStream*)File->IOStream->io)->memfree(File->IOStream->io, pSimpleTag->Value);
	}
	ArrayClear(SimpleTags);
}

static void releaseTags(array *Tags, MatroskaFile *File)
{
	struct Tag *pTag;

	for (pTag = ARRAYBEGIN(*Tags,struct Tag); pTag != ARRAYEND(*Tags,struct Tag); ++pTag)
	{
		releaseSimpleTags(&pTag->aSimpleTags, File);
		ArrayClear(&pTag->aTargets);
	}

	ArrayClear(Tags);
}

static void releaseTracks(array* tracks, MatroskaFile* File)
{
	struct TrackInfo* pTrack;
	for (pTrack = ARRAYBEGIN(*tracks, struct TrackInfo); pTrack != ARRAYEND(*tracks, struct TrackInfo); ++pTrack) {
		releaseTrackEntry(pTrack, (InputStream*)File->IOStream->io);
	}
	ArrayClear(tracks);
}

void mkv_CloseInput(MatroskaFile *File)
{
	if (File->Seg.Filename) ((InputStream*)File->IOStream->io)->memfree(File->IOStream->io, File->Seg.Filename);
	if (File->Seg.PrevFilename) ((InputStream*)File->IOStream->io)->memfree(File->IOStream->io, File->Seg.PrevFilename);
	if (File->Seg.NextFilename) ((InputStream*)File->IOStream->io)->memfree(File->IOStream->io, File->Seg.NextFilename);
	if (File->Seg.Title) ((InputStream*)File->IOStream->io)->memfree(File->IOStream->io, File->Seg.Title);
	if (File->Seg.MuxingApp) ((InputStream*)File->IOStream->io)->memfree(File->IOStream->io, File->Seg.MuxingApp);
	if (File->Seg.WritingApp) ((InputStream*)File->IOStream->io)->memfree(File->IOStream->io, File->Seg.WritingApp);

	releaseAttachments(&File->Attachments, File);
	releaseChapters(&File->Chapters, File);
	releaseTags(&File->Tags, File);
	releaseTracks(&File->Tracks,File);

	if (File->TrackList) NodeDelete((node*)File->TrackList);
//	ArrayClear(&File->Tracks);

	if (File->CueList) NodeDelete((node*)File->CueList);
	if (File->Segment_Info) NodeDelete((node*)File->Segment_Info);
	if (File->Segment) NodeDelete((node*)File->Segment);
	mkv_stream* istream = File->IOStream;
	void (*memfree)(struct InputStream* cc, void* mem) = ((InputStream*)File->IOStream->io)->memfree;
	memfree((InputStream*)File->IOStream->io,File);
	if (istream) NodeDelete(istream);
}

int mkv_ReadFrame(MatroskaFile *File, int mask, unsigned int *track, ulonglong *StartTime, ulonglong *EndTime, ulonglong *FilePos, unsigned int *FrameSize,
				void** FrameRef, unsigned int *FrameFlags)
{
	ebml_element *Elt = NULL,*Elt2;
	TrackInfo *tr;
	int16_t TrackNum;
	matroska_frame Frame;
	bool_t Skip;
	unsigned int Track;
	int UpperLevel = 0;

	if (FrameFlags)
		*FrameFlags = 0;
	if (!track)
		track = &Track;
	*track = 0xffffffff;

	for (;;)
	{
		if (!File->CurrentCluster)
		{
			for (;;)
			{
				if (!Elt)
					Elt = EBML_FindNextElement((stream*)File->IOStream,&File->L1Context,&UpperLevel,0);
				File->CurrentCluster = Elt;
				if (!File->CurrentCluster)
					return EOF;
				if (UpperLevel)
				{
					// TODO: changing segments not supported yet
					NodeDelete((node*)File->CurrentCluster);
					return EOF;
				}
				if (File->CurrentCluster->Context->Id == MATROSKA_ContextCluster.Id)
					break;

				Elt = File->CurrentCluster;
				File->CurrentCluster = EBML_ElementSkipData(File->CurrentCluster,(stream*)File->IOStream,&File->L1Context,NULL,1);
				NodeDelete((node*)Elt);
				Elt = NULL;
			}

			MATROSKA_LinkClusterReadSegmentInfo((matroska_cluster*)File->CurrentCluster,File->Segment_Info,1);
			File->ClusterContext.Context = &MATROSKA_ContextCluster;
			if (EBML_ElementIsFiniteSize(File->CurrentCluster))
				File->ClusterContext.EndPosition = EBML_ElementPositionEnd(File->CurrentCluster);
			else
				File->ClusterContext.EndPosition = INVALID_FILEPOS_T;
			File->ClusterContext.UpContext = &File->L1Context;

			File->CurrentBlock = NULL;
			File->CurrentFrame = 0;
		}

		Elt = NULL;
		while (!File->CurrentBlock)
		{
			if (!Elt)
			{
				UpperLevel = 0;
				Elt = EBML_FindNextElement((stream*)File->IOStream,&File->ClusterContext,&UpperLevel,1);
			}
			if (!Elt || UpperLevel>0)
			{
				NodeDelete((node*)File->CurrentCluster);
				File->CurrentCluster = NULL;
				--UpperLevel;
				break; // go to the next Cluster
			}

			Skip = 0;
			if (Elt->Context->Id == MATROSKA_ContextTimestamp.Id)
			{
				if (EBML_ElementReadData(Elt,(stream*)File->IOStream,&File->ClusterContext,1, SCOPE_ALL_DATA, 0)!=ERR_NONE)
					return EOF; // TODO: memory leak
				Elt2 = EBML_MasterFindFirstElt(File->CurrentCluster,&MATROSKA_ContextTimestamp,1,1);
				if (!Elt2)
					return EOF; // TODO: memory leak
				EBML_IntegerSetValue((ebml_integer*)Elt2,EBML_IntegerValue(Elt));
				NodeDelete((node*)Elt);
				Elt = NULL;
			}
			else if (Elt->Context->Id == MATROSKA_ContextSimpleBlock.Id)
			{
				if (EBML_ElementReadData(Elt,(stream*)File->IOStream,&File->ClusterContext,1, SCOPE_PARTIAL_DATA,0)!=ERR_NONE)
					return EOF; // TODO: memory leak

				TrackNum = MATROSKA_BlockTrackNum((matroska_block*)Elt);
				for (*track=0, tr=ARRAYBEGIN(File->Tracks,TrackInfo);tr!=ARRAYEND(File->Tracks,TrackInfo);++tr,(*track)++)
					if (TrackNum==tr->Number)
						break;
				if (tr==ARRAYEND(File->Tracks,TrackInfo) || (1<<*track & File->trackMask)!=0)
					Skip = 1;
				else
				{
					EBML_MasterAppend(File->CurrentCluster, Elt);
					MATROSKA_LinkBlockWithReadTracks((matroska_block*)Elt,File->TrackList,1);
					MATROSKA_LinkBlockReadSegmentInfo((matroska_block*)Elt,File->Segment_Info,1);
					File->CurrentBlock = (matroska_block*)Elt;
				}
			}
			else if (Elt->Context->Id == MATROSKA_ContextBlockGroup.Id)
			{
				if (EBML_ElementReadData(Elt,(stream*)File->IOStream,&File->ClusterContext,1, SCOPE_PARTIAL_DATA,0)!=ERR_NONE)
					return EOF; // TODO: memory leak

				Elt2 = EBML_MasterFindFirstElt(Elt, &MATROSKA_ContextCluster, 0, 0);
				if (!Elt2)
					Skip = 1;
				else
				{
					TrackNum = MATROSKA_BlockTrackNum((matroska_block*)Elt2);
					for (*track=0, tr=ARRAYBEGIN(File->Tracks,TrackInfo);tr!=ARRAYEND(File->Tracks,TrackInfo);++tr,(*track)++)
						if (TrackNum==tr->Number)
							break;

					if (tr==ARRAYEND(File->Tracks,TrackInfo) || (1<<*track & File->trackMask)!=0)
						Skip = 1;
					else
					{
						EBML_MasterAppend(File->CurrentCluster, Elt);
						MATROSKA_LinkBlockWithReadTracks((matroska_block*)Elt2,File->TrackList,1);
						MATROSKA_LinkBlockReadSegmentInfo((matroska_block*)Elt2,File->Segment_Info,1);
						File->CurrentBlock = (matroska_block*)Elt2;
					}
				}
			}
			else Skip = 1;

			if (Skip)
			{
				Elt2 = Elt;
				Elt = EBML_ElementSkipData(Elt2,(stream*)File->IOStream,&File->L1Context,NULL,1);
				NodeDelete((node*)Elt2);
			}
		}
		if (File->CurrentCluster && File->CurrentBlock)
			break;
	}

	assert(File->CurrentBlock!=NULL);
	if (MATROSKA_BlockGetFrame(File->CurrentBlock,File->CurrentFrame,&Frame,0)!=ERR_NONE)
		return -1; // TODO: memory leaks

	if (*track==(unsigned int)-1)
	{
		TrackNum = MATROSKA_BlockTrackNum(File->CurrentBlock);
		for (*track=0, tr=ARRAYBEGIN(File->Tracks,TrackInfo);tr!=ARRAYEND(File->Tracks,TrackInfo);++tr,(*track)++)
			if (TrackNum==tr->Number)
				break;
	}

	if (Frame.Timecode!=INVALID_TIMECODE_T)
	{
		if (StartTime)
			*StartTime = Frame.Timecode;
		if (EndTime && Frame.Duration != INVALID_TIMECODE_T)
			*EndTime = Frame.Timecode + Frame.Duration;
	}

	if (FilePos)
		*FilePos = ((ebml_element*)File->CurrentBlock)->ElementPosition;
	if (FrameFlags)
	{
		if (Frame.Timecode == INVALID_TIMECODE_T)
			*FrameFlags |= FRAME_UNKNOWN_START;
		if (Frame.Duration == INVALID_TIMECODE_T)
			*FrameFlags |= FRAME_UNKNOWN_END;
		if (MATROSKA_BlockKeyframe(File->CurrentBlock))
			*FrameFlags |= FRAME_KF;
	}
	MATROSKA_BlockSkipToFrame(File->CurrentBlock, (stream*)File->IOStream, File->CurrentFrame);
	unsigned int size = MATROSKA_BlockGetLength(File->CurrentBlock, File->CurrentFrame++);
	*FrameRef = ((InputStream*)File->IOStream->io)->makeref(File->IOStream->io, size);
	*FrameSize = size;
	if (File->CurrentFrame >= MATROSKA_BlockGetFrameCount(File->CurrentBlock))
	{
		File->CurrentBlock = NULL;
		File->CurrentFrame = 0;
	}

	return 0;
}

static void SeekToPos(MatroskaFile *File, filepos_t SeekPos)
{
	if (File->CurrentBlock)
	{
		NodeDelete((node*)File->CurrentBlock);
		File->CurrentBlock = NULL;
	}
	File->CurrentFrame = 0;
	if (File->CurrentCluster && File->CurrentCluster->ElementPosition!=SeekPos)
	{
		NodeDelete((node*)File->CurrentCluster);
		File->CurrentCluster = NULL;
	}
	((InputStream*)File->IOStream->io)->ioseek(File->IOStream->io,SeekPos,SEEK_SET);
}

void mkv_Seek(MatroskaFile *File, timecode_t timecode, int flags)
{
	matroska_cuepoint *CuePoint;
	filepos_t SeekPos;

	if (File->flags & MKVF_AVOID_SEEKS || File->pFirstCluster==INVALID_FILEPOS_T || timecode==INVALID_TIMECODE_T)
		return;

	if (timecode==0)
	{
		SeekToPos(File, File->pFirstCluster);
		return;
	}
	if (!File->CueList)
		return;

	CuePoint = MATROSKA_CuesGetTimecodeStart(File->CueList,timecode);
	if (CuePoint==NULL)
		return;

	SeekPos = MATROSKA_CuePosInSegment(CuePoint) + EBML_ElementPositionData(File->Segment);
	SeekToPos(File, SeekPos);
}

int mkv_TruncFloat(float f)
{
	return (int)f;
}

static filepos_t Seek(haali_stream *p ,filepos_t Pos,int SeekMode)
{
	if (!(Pos == 0 && SeekMode==SEEK_CUR))
		p->io->ioseek(p->io,Pos,SeekMode);
	return p->io->iotell(p->io);
}

static err_t Read(haali_stream* p,void* Data,size_t Size,size_t* pReaded)
{
	size_t Readed;
	if (!pReaded)
		pReaded = &Readed;
	*pReaded = p->io->ioread(p->io,Data,Size);
	if (Size && *pReaded!=Size)
		return ERR_END_OF_FILE;
	return ERR_NONE;
}

META_START(HaaliStream_Class,HAALI_STREAM_CLASS)
META_CLASS(SIZE,sizeof(haali_stream))
META_VMT(TYPE_FUNC,stream_vmt,Seek,Seek)
META_VMT(TYPE_FUNC,stream_vmt,Read,Read)
META_END(STREAM_CLASS)
