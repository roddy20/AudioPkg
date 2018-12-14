/*
 * File: HdaControllerHdaIo.c
 *
 * Copyright (c) 2018 John Davis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "HdaController.h"
#include <Library/HdaRegisters.h>

// HDA I/O Device Path GUID.
EFI_GUID gEfiHdaIoDevicePathGuid = EFI_HDA_IO_DEVICE_PATH_GUID;

/**                                                                 
  Retrieves this codec's address.

  @param[in]  This              A pointer to the HDA_IO_PROTOCOL instance.
  @param[out] CodecAddress      The codec's address.

  @retval EFI_SUCCESS           The codec's address was returned.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.                      
**/
EFI_STATUS
EFIAPI
HdaControllerHdaIoGetAddress(
    IN  EFI_HDA_IO_PROTOCOL *This,
    OUT UINT8 *CodecAddress) {
    HDA_IO_PRIVATE_DATA *HdaPrivateData;

    // If parameters are NULL, return error.
    if (This == NULL || CodecAddress == NULL)
        return EFI_INVALID_PARAMETER;

    // Get private data and codec address.
    HdaPrivateData = HDA_IO_PRIVATE_DATA_FROM_THIS(This);
    *CodecAddress = HdaPrivateData->HdaCodecAddress;
    return EFI_SUCCESS;
}

/**                                                                 
  Sends a single command to the codec.

  @param[in]  This              A pointer to the HDA_IO_PROTOCOL instance.
  @param[in]  Node              The destination node.
  @param[in]  Verb              The verb to send.
  @param[out] Response          The response received.

  @retval EFI_SUCCESS           The verb was sent successfully and a response received.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.                      
**/
EFI_STATUS
EFIAPI
HdaControllerHdaIoSendCommand(
    IN  EFI_HDA_IO_PROTOCOL *This,
    IN  UINT8 Node,
    IN  UINT32 Verb,
    OUT UINT32 *Response) {

    // Create verb list with single item.
    EFI_HDA_IO_VERB_LIST HdaCodecVerbList;
    HdaCodecVerbList.Count = 1;
    HdaCodecVerbList.Verbs = &Verb;
    HdaCodecVerbList.Responses = Response;

    // Call SendCommands().
    return HdaControllerHdaIoSendCommands(This, Node, &HdaCodecVerbList);
}

/**                                                                 
  Sends a set of commands to the codec.

  @param[in] This               A pointer to the HDA_IO_PROTOCOL instance.
  @param[in] Node               The destination node.
  @param[in] Verbs              The verbs to send. Responses will be delievered in the same list.

  @retval EFI_SUCCESS           The verbs were sent successfully and all responses received.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.                      
**/
EFI_STATUS
EFIAPI
HdaControllerHdaIoSendCommands(
    IN EFI_HDA_IO_PROTOCOL *This,
    IN UINT8 Node,
    IN EFI_HDA_IO_VERB_LIST *Verbs) {
    // Create variables.
    HDA_IO_PRIVATE_DATA *HdaPrivateData;

    // If parameters are NULL, return error.
    if (This == NULL || Verbs == NULL)
        return EFI_INVALID_PARAMETER;

    // Get private data and send commands.
    HdaPrivateData = HDA_IO_PRIVATE_DATA_FROM_THIS(This);
    return HdaControllerSendCommands(HdaPrivateData->HdaControllerDev, HdaPrivateData->HdaCodecAddress, Node, Verbs);
}

EFI_STATUS
EFIAPI
HdaControllerHdaIoSetupStream(
    IN  EFI_HDA_IO_PROTOCOL *This,
    IN  EFI_HDA_IO_PROTOCOL_TYPE Type,
    IN  UINT16 Format,
    OUT UINT8 *StreamId) {
    //DEBUG((DEBUG_INFO, "HdaControllerHdaIoSetupStream(): start\n"));

    // Create variables.
    EFI_STATUS Status;
    HDA_IO_PRIVATE_DATA *HdaIoPrivateData;
    HDA_CONTROLLER_DEV *HdaControllerDev;
    EFI_PCI_IO_PROTOCOL *PciIo;

    // Stream.
    HDA_STREAM *HdaStream;
    UINT8 HdaStreamId;
    EFI_TPL OldTpl = 0;

    // If a parameter is invalid, return error.
    if ((This == NULL) || (Type >= EfiHdaIoTypeMaximum) || (StreamId == NULL))
        return EFI_INVALID_PARAMETER;

    // Get private data.
    HdaIoPrivateData = HDA_IO_PRIVATE_DATA_FROM_THIS(This);
    HdaControllerDev = HdaIoPrivateData->HdaControllerDev;
    PciIo = HdaControllerDev->PciIo;

    // Get stream.
    if (Type == EfiHdaIoTypeOutput)
        HdaStream = HdaIoPrivateData->HdaOutputStream;
    else
        HdaStream = HdaIoPrivateData->HdaInputStream;

    // Get current stream ID.
    Status = HdaControllerGetStreamId(HdaStream, &HdaStreamId);
    if (EFI_ERROR(Status))
        goto DONE;

    // Is a stream ID allocated already? If so that means the stream is already
    // set up and we'll need to tear it down first.
    if (HdaStreamId > 0) {
        Status = EFI_ALREADY_STARTED;
        goto DONE;
    }

    // Raise TPL so we can't be messed with.
    OldTpl = gBS->RaiseTPL(TPL_HIGH_LEVEL);

    // Find and allocate stream ID.
    for (UINT8 i = HDA_STREAM_ID_MIN; i <= HDA_STREAM_ID_MAX; i++) {
        if (!(HdaControllerDev->StreamIdMapping & (1 << i))) {
            HdaControllerDev->StreamIdMapping |= (1 << i);
            HdaStreamId = i;
            break;
        }
    }

    // If stream ID is still zero, fail.
    if (HdaStreamId == 0) {
        Status = EFI_OUT_OF_RESOURCES;
        goto DONE;
    }

    // Set stream ID.
    Status = HdaControllerSetStreamId(HdaIoPrivateData->HdaOutputStream, HdaStreamId);
    if (EFI_ERROR(Status))
        goto DONE;
    *StreamId = HdaStreamId;

    // Set stream format.
    //DEBUG((DEBUG_INFO, "HdaControllerHdaIoSetupStream(): setting format 0x%X\n", Format));
    Status = PciIo->Mem.Write(PciIo, EfiPciIoWidthUint16, PCI_HDA_BAR,
        HDA_REG_SDNFMT(HdaStream->Index), 1, &Format);
    if (EFI_ERROR(Status))
        goto DONE;

    // Stream is ready.
    Status = EFI_SUCCESS;

DONE:
    // Restore TPL if needed.
    if (OldTpl)
        gBS->RestoreTPL(OldTpl);

    return Status;
}

EFI_STATUS
EFIAPI
HdaControllerHdaIoCloseStream(
    IN EFI_HDA_IO_PROTOCOL *This,
    IN EFI_HDA_IO_PROTOCOL_TYPE Type) {
    //DEBUG((DEBUG_INFO, "HdaControllerHdaIoCloseStream(): start\n"));

    // Create variables.
    EFI_STATUS Status;
    HDA_IO_PRIVATE_DATA *HdaIoPrivateData;
    HDA_CONTROLLER_DEV *HdaControllerDev;

    // Stream.
    HDA_STREAM *HdaStream;
    UINT8 HdaStreamId;
    EFI_TPL OldTpl = 0;

    // If a parameter is invalid, return error.
    if ((This == NULL) || (Type >= EfiHdaIoTypeMaximum))
        return EFI_INVALID_PARAMETER;

    // Get private data.
    HdaIoPrivateData = HDA_IO_PRIVATE_DATA_FROM_THIS(This);
    HdaControllerDev = HdaIoPrivateData->HdaControllerDev;

    // Get stream.
    if (Type == EfiHdaIoTypeOutput)
        HdaStream = HdaIoPrivateData->HdaOutputStream;
    else
        HdaStream = HdaIoPrivateData->HdaInputStream;

    // Get current stream ID.
    Status = HdaControllerGetStreamId(HdaStream, &HdaStreamId);
    if (EFI_ERROR(Status))
        goto DONE;

    // Is a stream ID already at zero?
    if (HdaStreamId == 0) {
        Status = EFI_SUCCESS;
        goto DONE;
    }

    // Raise TPL so we can't be messed with.
    OldTpl = gBS->RaiseTPL(TPL_HIGH_LEVEL);

    // Stop stream.
    Status = HdaControllerHdaIoStopStream(This, Type);
    if (EFI_ERROR(Status))
        goto DONE;

    // Set stream ID to zero.
    Status = HdaControllerSetStreamId(HdaStream, 0);
    if (EFI_ERROR(Status))
        goto DONE;

    // De-allocate stream ID from bitmap.
    HdaControllerDev->StreamIdMapping &= ~(1 << HdaStreamId);

    // Stream closed successfully.
    Status = EFI_SUCCESS;

DONE:
    // Restore TPL if needed.
    if (OldTpl)
        gBS->RestoreTPL(OldTpl);

    return Status;
}

EFI_STATUS
EFIAPI
HdaControllerHdaIoGetStream(
    IN  EFI_HDA_IO_PROTOCOL *This,
    IN  EFI_HDA_IO_PROTOCOL_TYPE Type,
    OUT BOOLEAN *State) {
    //DEBUG((DEBUG_INFO, "HdaControllerHdaIoGetStream(): start\n"));

    // Create variables.
    HDA_IO_PRIVATE_DATA *HdaIoPrivateData;
    HDA_STREAM *HdaStream;

    // If a parameter is invalid, return error.
    if ((This == NULL) || (Type >= EfiHdaIoTypeMaximum) || (State == NULL))
        return EFI_INVALID_PARAMETER;

    // Get private data.
    HdaIoPrivateData = HDA_IO_PRIVATE_DATA_FROM_THIS(This);

    // Get stream.
    if (Type == EfiHdaIoTypeOutput)
        HdaStream = HdaIoPrivateData->HdaOutputStream;
    else
        HdaStream = HdaIoPrivateData->HdaInputStream;

    // Get stream state.
    return HdaControllerGetStream(HdaStream, State);
}

EFI_STATUS
EFIAPI
HdaControllerHdaIoStartStream(
    IN EFI_HDA_IO_PROTOCOL *This,
    IN EFI_HDA_IO_PROTOCOL_TYPE Type,
    IN VOID *Buffer,
    IN UINTN BufferLength,
    IN UINTN BufferPosition OPTIONAL,
    IN EFI_HDA_IO_STREAM_CALLBACK Callback OPTIONAL,
    IN VOID *Context1 OPTIONAL,
    IN VOID *Context2 OPTIONAL,
    IN VOID *Context3 OPTIONAL) {
    //DEBUG((DEBUG_INFO, "HdaControllerHdaIoStartStream(): start\n"));

    // Create variables.
    EFI_STATUS Status;
    HDA_IO_PRIVATE_DATA *HdaIoPrivateData;
    HDA_CONTROLLER_DEV *HdaControllerDev;
    EFI_PCI_IO_PROTOCOL *PciIo;

    // Stream.
    HDA_STREAM *HdaStream;
    UINT8 HdaStreamId;
    UINT16 HdaStreamSts;
    UINT32 HdaStreamDmaPos;
    UINTN HdaStreamDmaRemainingLength;

    // If a parameter is invalid, return error.
    if ((This == NULL) || (Type >= EfiHdaIoTypeMaximum) ||
        (Buffer == NULL) || (BufferLength == 0) || (BufferPosition >= BufferLength))
        return EFI_INVALID_PARAMETER;

    // Get private data.
    HdaIoPrivateData = HDA_IO_PRIVATE_DATA_FROM_THIS(This);
    HdaControllerDev = HdaIoPrivateData->HdaControllerDev;
    PciIo = HdaControllerDev->PciIo;

    // Get stream.
    if (Type == EfiHdaIoTypeOutput)
        HdaStream = HdaIoPrivateData->HdaOutputStream;
    else
        HdaStream = HdaIoPrivateData->HdaInputStream;

    // Get current stream ID.
    Status = HdaControllerGetStreamId(HdaStream, &HdaStreamId);
    if (EFI_ERROR(Status))
        return Status;

    // Is a stream ID zero? If so that means the stream is not setup yet.
    if (HdaStreamId == 0)
        return EFI_NOT_READY;

    // Reset completion bit.
    HdaStreamSts = HDA_REG_SDNSTS_BCIS;
    Status = PciIo->Mem.Write(PciIo, EfiPciIoWidthUint8, PCI_HDA_BAR, HDA_REG_SDNSTS(HdaStream->Index), 1, &HdaStreamSts);
    if (EFI_ERROR(Status))
        return Status;

    // Get current DMA position.
    HdaStreamDmaPos = HdaControllerDev->DmaPositions[HdaStream->Index].Position;
    DEBUG((DEBUG_INFO, "HdaControllerHdaIoStartStream(): stream %u DMA pos 0x%X\n",
        HdaStream->Index, HdaStreamDmaPos));

    // Save pointer to buffer.
    HdaStream->BufferSource = Buffer;
    HdaStream->BufferSourceLength = BufferLength;
    HdaStream->BufferSourcePosition = BufferPosition;
    HdaStream->Callback = Callback;
    HdaStream->CallbackContext1 = Context1;
    HdaStream->CallbackContext2 = Context2;
    HdaStream->CallbackContext3 = Context3;

    // Zero out buffer.
    ZeroMem(HdaStream->BufferData, HDA_STREAM_BUF_SIZE);

    // Determine number of bytes to write. If the stream has never run before (LPIB is 0), fill the whole buffer.
    HdaStreamDmaRemainingLength = HDA_STREAM_BUF_SIZE - HdaStreamDmaPos;
    if ((HdaStream->BufferSourcePosition + HdaStreamDmaRemainingLength) > BufferLength)
        HdaStreamDmaRemainingLength = BufferLength;

    // Fill stream buffer.
    CopyMem(HdaStream->BufferData + HdaStreamDmaPos, HdaStream->BufferSource + HdaStream->BufferSourcePosition, HdaStreamDmaRemainingLength);
    HdaStream->BufferSourcePosition += HdaStreamDmaRemainingLength;
    DEBUG((DEBUG_INFO, "%u (0x%X) bytes written\n", HdaStreamDmaRemainingLength, HdaStreamDmaRemainingLength));

    // If we are starting in the upper half, fill the lower half as well.
    if ((HdaStreamDmaPos >= HDA_STREAM_BUF_SIZE_HALF) && (HdaStreamDmaRemainingLength <= HDA_STREAM_BUF_SIZE_HALF)) {
        HdaStreamDmaRemainingLength = BufferLength - HdaStream->BufferSourcePosition;
        if (HdaStreamDmaRemainingLength > HDA_STREAM_BUF_SIZE_HALF)
            HdaStreamDmaRemainingLength = HDA_STREAM_BUF_SIZE_HALF;
        CopyMem(HdaStream->BufferData, HdaStream->BufferSource + HdaStream->BufferSourcePosition, HdaStreamDmaRemainingLength);
        HdaStream->BufferSourcePosition += HdaStreamDmaRemainingLength;
    }

    // Setup polling timer.
    Status = gBS->SetTimer(HdaStream->PollTimer, TimerPeriodic, HDA_STREAM_POLL_TIME);
    if (EFI_ERROR(Status))
        goto STOP_STREAM;

    // Change stream state.
    Status = HdaControllerSetStream(HdaStream, TRUE);
    if (EFI_ERROR(Status))
        goto STOP_STREAM;
    return EFI_SUCCESS;

STOP_STREAM:
    // Stop stream.
    HdaControllerHdaIoStopStream(This, Type);
    return Status;
}

EFI_STATUS
EFIAPI
HdaControllerHdaIoStopStream(
    IN EFI_HDA_IO_PROTOCOL *This,
    IN EFI_HDA_IO_PROTOCOL_TYPE Type) {
    //DEBUG((DEBUG_INFO, "HdaControllerHdaIoStopStream(): start\n"));

    // Create variables.
    EFI_STATUS Status;
    HDA_IO_PRIVATE_DATA *HdaIoPrivateData;
    HDA_CONTROLLER_DEV *HdaControllerDev;

    // Stream.
    HDA_STREAM *HdaStream;
    UINT8 HdaStreamId;

    // If a parameter is invalid, return error.
    if ((This == NULL) || (Type >= EfiHdaIoTypeMaximum))
        return EFI_INVALID_PARAMETER;

    // Get private data.
    HdaIoPrivateData = HDA_IO_PRIVATE_DATA_FROM_THIS(This);
    HdaControllerDev = HdaIoPrivateData->HdaControllerDev;

    // Get stream.
    if (Type == EfiHdaIoTypeOutput)
        HdaStream = HdaIoPrivateData->HdaOutputStream;
    else
        HdaStream = HdaIoPrivateData->HdaInputStream;

    // Get current stream ID.
    Status = HdaControllerGetStreamId(HdaStream, &HdaStreamId);
    if (EFI_ERROR(Status))
        return Status;

    // Is the stream ID zero? If so that means the stream is not setup yet.
    if (HdaStreamId == 0)
        return EFI_NOT_READY;

    // Cancel polling timer.
    Status = gBS->SetTimer(HdaStream->PollTimer, TimerCancel, 0);
    if (EFI_ERROR(Status))
        return Status;

    // Stop stream.
    Status = HdaControllerSetStream(HdaStream, FALSE);
    if (EFI_ERROR(Status))
        return Status;

    // Remove source buffer pointer.
    HdaStream->BufferSource = NULL;
    HdaStream->BufferSourceLength = 0;
    HdaStream->BufferSourcePosition = 0;
    HdaStream->Callback = NULL;
    HdaStream->CallbackContext1 = NULL;
    HdaStream->CallbackContext2 = NULL;
    HdaStream->CallbackContext3 = NULL;
    return EFI_SUCCESS;
}