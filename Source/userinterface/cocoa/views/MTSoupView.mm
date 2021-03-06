//
//  MTSoupView.mm
//  MacTierra
//
//  Created by Simon Fraser on 8/16/08.
//  Copyright 2008 __MyCompanyName__. All rights reserved.
//

#import <algorithm>

#import "MTSoupView.h"

#import "NSArrayAdditions.h"

#import "MT_Cellmap.h"
#import "MT_Soup.h"
#import "MT_World.h"

#import "MTCreature.h"
#import "MTInventoryGenotype.h"
#import "MTWorldController.h"

using namespace MacTierra;

@interface MTSoupView(Private)

- (void)setScalingCTM;
- (void)drawCells:(NSRect)inDirtyRect;
- (void)drawFecundity:(NSRect)inDirtyRect;
- (void)drawInstructionPointers:(NSRect)inDirtyRect;

- (CGRect)soupRect;
- (CGPoint)soupAddressToSoupPoint:(address_t)inAddr;
- (address_t)soupPointToSoupAddress:(CGPoint)inPoint;

- (CGAffineTransform)viewToSoupTransform;
- (CGAffineTransform)soupToViewTransform;

- (CGPoint)viewPointToSoupPoint:(CGPoint)inPoint;
- (CGPoint)soupPointToViewPoint:(CGPoint)inPoint;

- (void)startObservingSelectedGenotypes;
- (void)stopObservingSelectedGenotypes;

@end

#pragma mark -

@implementation MTSoupView

@synthesize zoomToFit;
@synthesize showCells;
@synthesize showInstructionPointers;
@synthesize showFecundity;
@synthesize focusedCreatureName;

- (id)initWithFrame:(NSRect)inFrame
{
    if ((self = [super initWithFrame:inFrame]))
    {
        zoomToFit = YES;
        showCells = NO;
        showInstructionPointers = NO;
        self.focusedCreatureName = @"";

        [self registerForDraggedTypes:[NSArray arrayWithObjects:kGenotypeDataPasteboardType, NSStringPboardType, nil]];
    }
    return self;
}

- (void)dealloc
{
    self.focusedCreatureName = nil;
    [super dealloc];
}

- (void)awakeFromNib
{
    [self startObservingSelectedGenotypes];
}

- (void)setWorld:(MacTierra::World*)inWorld
{
    mWorld = inWorld;
    
    if (mWorld)
    {
        const int kSoupWidth = 512;
        mSoupWidth = kSoupWidth;
        mSoupHeight = mWorld->soupSize() / kSoupWidth;
        
        [self setGLOptions];
    }

    [self setNeedsDisplay:YES];
    [[self window] invalidateCursorRectsForView:self];
}

- (MacTierra::World*)world
{
    return mWorld;
}

- (void)setShowCells:(BOOL)inShow
{
    if (inShow != showCells)
    {
        [self willChangeValueForKey:@"showCells"];
        showCells = inShow;
        [self setNeedsDisplay:YES];
        [self didChangeValueForKey:@"showCells"];
    }
}

- (void)setShowInstructionPointers:(BOOL)inShow
{
    if (inShow != showInstructionPointers)
    {
        [self willChangeValueForKey:@"showInstructionPointers"];
        showInstructionPointers = inShow;
        [self setNeedsDisplay:YES];
        [self didChangeValueForKey:@"showInstructionPointers"];
    }
}

- (void)setShowFecundity:(BOOL)inShow
{
    if (inShow != showFecundity)
    {
        [self willChangeValueForKey:@"showFecundity"];
        showFecundity = inShow;
        [self setNeedsDisplay:YES];
        [self didChangeValueForKey:@"showFecundity"];
    }
}

- (void)viewDidMoveToWindow
{
    if ([self window])
        [[self window] setAcceptsMouseMovedEvents:YES];
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)isFlipped
{
    return YES;
}

- (void)drawRect:(NSRect)inDirtyRect
{
    [mWorldController lockWorld];

    [super drawRect:inDirtyRect];
    
    [NSGraphicsContext saveGraphicsState];
    [self setScalingCTM];

    if (showInstructionPointers || showFecundity || showCells)
    {
        [[NSColor colorWithCalibratedWhite:0.0f alpha:0.5f] set];
        NSRectFill(NSMakeRect(0, 0, mSoupWidth, mSoupHeight));
    }
    
    if (showFecundity)
        [self drawFecundity:inDirtyRect];
    else if (showCells)
        [self drawCells:inDirtyRect];

    if (showInstructionPointers)
        [self drawInstructionPointers:inDirtyRect];

    [NSGraphicsContext restoreGraphicsState];

    [mWorldController unlockWorld];
}

- (void)setScalingCTM
{
    // set the CTM to match the zooming that GL does
    CGContextRef cgContext = (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort];
    CGContextConcatCTM(cgContext, [self soupToViewTransform]);
}

- (void)drawCells:(NSRect)inDirtyRect
{
    if (!mWorld)
        return;

    MTInventoryGenotype* selectedGenotype = nil;
    if ([[mGenotypesArrayController selectedObjects] count] == 1)
    {
        selectedGenotype = [[mGenotypesArrayController selectedObjects] firstObject];
    }
    
    CGContextRef cgContext = (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort];

    NSColor* adultColor  = [[NSColor blueColor] colorWithAlphaComponent:0.5];
    NSColor* embryoColor = [[NSColor grayColor] colorWithAlphaComponent:0.5];
    NSColor* selectedGenotypeColor = [[NSColor orangeColor] colorWithAlphaComponent:0.9];

    CGContextSetLineWidth(cgContext, 1.0f);
    
    CellMap*    cellMap = mWorld->cellMap();
    const u_int32_t soupSize = mWorld->soupSize();
    
    CellMap::CreatureList::const_iterator iterEnd = cellMap->cells().end();
    for (CellMap::CreatureList::const_iterator it = cellMap->cells().begin(); it != iterEnd; ++it)
    {
        const CreatureRange& curCell = *it;
        const Creature* curCreature = curCell.mData;
        
        int startLine   = curCell.start() / mSoupWidth;
        int endLine     = curCell.wrappedEnd(soupSize) / mSoupWidth;
        
        int startCol    = curCell.start() % mSoupWidth;
        int endCol      = curCell.wrappedEnd(soupSize) % mSoupWidth;
        
        CGContextBeginPath(cgContext);
        
        if (curCell.wraps(soupSize))
        {
            int numLines = mSoupHeight;
            
            for (int i = startLine; i < numLines; ++i)
            {
                CGPoint startPoint = CGPointMake((i == startLine) ? startCol : 0, i);
                CGPoint endPoint   = CGPointMake(mSoupWidth, i);
                
                CGContextMoveToPoint(cgContext, startPoint.x, startPoint.y + 0.5);
                CGContextAddLineToPoint(cgContext, endPoint.x, endPoint.y + 0.5);
            }

            for (int i = 0; i <= endLine; ++i)
            {
                CGPoint startPoint = CGPointMake(0, i);
                CGPoint endPoint   = CGPointMake((i == endLine) ? endCol : mSoupWidth, i);
                
                CGContextMoveToPoint(cgContext, startPoint.x, startPoint.y + 0.5);
                CGContextAddLineToPoint(cgContext, endPoint.x, endPoint.y + 0.5);
            }
        }
        else
        {
            for (int i = startLine; i <= endLine; ++i)
            {
                CGPoint startPoint = CGPointMake((i == startLine) ? startCol : 0, i);
                CGPoint endPoint   = CGPointMake((i == endLine) ? endCol : mSoupWidth, i);
                
                CGContextMoveToPoint(cgContext, startPoint.x, startPoint.y + 0.5);
                CGContextAddLineToPoint(cgContext, endPoint.x, endPoint.y + 0.5);
            }
        }

        if (curCreature->isEmbryo())
            [embryoColor set];
        else
        {
            if (curCreature->genotype() == selectedGenotype.genotype)
                [selectedGenotypeColor set];
            else
                [adultColor set];
        }
        
        CGContextStrokePath(cgContext);
    }
}

- (NSColor*)colorForOffspring:(NSInteger)inNumOffspring identicalOffspring:(NSInteger)inIdenticalOffspring
{
    // TODO: blend of yellow and red
    
    const float kSingleOffspringOpacity = 0.3;
    const NSInteger kMaxFecundity = 10;

    float opacity = kSingleOffspringOpacity + (1.0 - kSingleOffspringOpacity) * (std::min(inNumOffspring, kMaxFecundity) / (float)kMaxFecundity);
    float identicalFraction = std::min(inIdenticalOffspring, kMaxFecundity) / (float)kMaxFecundity;
    
    return [NSColor colorWithCalibratedRed:1.0 green:identicalFraction blue:0 alpha:opacity];
}

- (void)drawFecundity:(NSRect)inDirtyRect
{
    if (!mWorld)
        return;

    CGContextRef cgContext = (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort];

    CGContextSetLineWidth(cgContext, 1.0f);
    
    CellMap*    cellMap = mWorld->cellMap();
    const u_int32_t soupSize = mWorld->soupSize();
    
    CellMap::CreatureList::const_iterator iterEnd = cellMap->cells().end();
    for (CellMap::CreatureList::const_iterator it = cellMap->cells().begin(); it != iterEnd; ++it)
    {
        const CreatureRange& curCell = *it;
        const Creature* curCreature = curCell.mData;
        
        if (curCreature->isEmbryo() || curCreature->numOffspring() == 0)
            continue;
        
        int startLine   = curCell.start() / mSoupWidth;
        int endLine     = curCell.wrappedEnd(soupSize) / mSoupWidth;
        
        int startCol    = curCell.start() % mSoupWidth;
        int endCol      = curCell.wrappedEnd(soupSize) % mSoupWidth;
        
        CGContextBeginPath(cgContext);
        
        if (curCell.wraps(soupSize))
        {
            int numLines = mSoupHeight;
            
            for (int i = startLine; i < numLines; ++i)
            {
                CGPoint startPoint = CGPointMake((i == startLine) ? startCol : 0, i);
                CGPoint endPoint   = CGPointMake(mSoupWidth, i);
                
                CGContextMoveToPoint(cgContext, startPoint.x, startPoint.y + 0.5);
                CGContextAddLineToPoint(cgContext, endPoint.x, endPoint.y + 0.5);
            }

            for (int i = 0; i <= endLine; ++i)
            {
                CGPoint startPoint = CGPointMake(0, i);
                CGPoint endPoint   = CGPointMake((i == endLine) ? endCol : mSoupWidth, i);
                
                CGContextMoveToPoint(cgContext, startPoint.x, startPoint.y + 0.5);
                CGContextAddLineToPoint(cgContext, endPoint.x, endPoint.y + 0.5);
            }
        }
        else
        {
            for (int i = startLine; i <= endLine; ++i)
            {
                CGPoint startPoint = CGPointMake((i == startLine) ? startCol : 0, i);
                CGPoint endPoint   = CGPointMake((i == endLine) ? endCol : mSoupWidth, i);
                
                CGContextMoveToPoint(cgContext, startPoint.x, startPoint.y + 0.5);
                CGContextAddLineToPoint(cgContext, endPoint.x, endPoint.y + 0.5);
            }
        }

        [[self colorForOffspring:curCreature->numOffspring() identicalOffspring:curCreature->numIdenticalOffspring()] set];
        CGContextStrokePath(cgContext);
    }
}

- (void)drawInstructionPointers:(NSRect)inDirtyRect
{
    if (!mWorld)
        return;

    CGContextRef cgContext = (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort];

    const CGFloat kInstPointerRectBorderWidth = 1.0f;
    CGContextSetLineWidth(cgContext, kInstPointerRectBorderWidth);
    
    NSColor* withinColor  = [[NSColor greenColor] colorWithAlphaComponent:0.5];
    NSColor* outsideColor = [[NSColor orangeColor] colorWithAlphaComponent:0.5];
    
    CellMap*    cellMap = mWorld->cellMap();
    const u_int32_t soupSize = mWorld->soupSize();
    
    CellMap::CreatureList::const_iterator iterEnd = cellMap->cells().end();
    for (CellMap::CreatureList::const_iterator it = cellMap->cells().begin(); it != iterEnd; ++it)
    {
        const CreatureRange& curCell = *it;
        const Creature* curCreature = curCell.mData;
        if (curCreature->isEmbryo())
            continue;

        address_t instPointer = curCreature->referencedLocation();
        bool isInCreature = curCreature->containsAddress(instPointer, soupSize);

        if (isInCreature)
            [withinColor set];
        else
            [outsideColor set];
        
        CGRect pointerRect = CGRectMake(instPointer % mSoupWidth - kInstPointerRectBorderWidth,
                                        instPointer / mSoupWidth - kInstPointerRectBorderWidth,
                                        1 + 2 * kInstPointerRectBorderWidth,
                                        1 + 2 * kInstPointerRectBorderWidth);
        CGContextStrokeEllipseInRect(cgContext, pointerRect);
    }

}

- (CGPoint)soupAddressToSoupPoint:(address_t)inAddr
{
    return CGPointMake(inAddr % mSoupWidth, inAddr / mSoupWidth);
}

- (address_t)soupPointToSoupAddress:(CGPoint)inPoint
{
    return ((address_t)inPoint.x + (address_t)inPoint.y * mSoupWidth);
}

- (CGAffineTransform)soupToViewTransform
{
    return zoomToFit ? CGAffineTransformMakeScale((CGFloat)mGlWidth / mSoupWidth, (CGFloat)mGlHeight / mSoupHeight)
                     : CGAffineTransformMakeTranslation((mGlWidth - (float)mSoupWidth) / 2.0, (mGlHeight - (float)mSoupHeight) / 2.0);
}

- (CGAffineTransform)viewToSoupTransform
{
    return CGAffineTransformInvert([self soupToViewTransform]);
}

- (CGRect)soupRect
{
    CGRect soupExtent = zoomToFit ? CGRectMake(0, 0, mGlWidth, mGlHeight)
                                  : CGRectMake((mGlWidth - (float)mSoupWidth) / 2.0, (mGlHeight - (float)mSoupHeight) / 2.0, mSoupWidth, mSoupHeight);
    return soupExtent;
}

- (BOOL)viewPointInSoup:(CGPoint)inPoint
{
    return CGRectContainsPoint([self soupRect], inPoint);
}

- (CGPoint)viewPointToSoupPoint:(CGPoint)inPoint
{
    if (![self viewPointInSoup:inPoint])
        return CGPointZero;

    return CGPointApplyAffineTransform(inPoint, [self viewToSoupTransform]);
}

- (CGPoint)soupPointToViewPoint:(CGPoint)inPoint
{
    return CGPointApplyAffineTransform(inPoint, [self soupToViewTransform]);
}

- (void)setZoomToFit:(BOOL)inZoom
{
    if (inZoom != zoomToFit)
    {
        [[self window] invalidateCursorRectsForView:self];
        [self setNeedsDisplay:YES];
        zoomToFit = inZoom;
    }
}

#pragma mark -

- (MacTierra::Creature*)creatureForPoint:(NSPoint)inLocalPoint
{
    CGPoint thePoint = *(CGPoint*)&inLocalPoint;
    if (mWorld && [self viewPointInSoup:thePoint])
    {
        CGPoint soupPoint = [self viewPointToSoupPoint:thePoint];
        address_t soupAddr = [self soupPointToSoupAddress:soupPoint];

        MacTierra::Creature* theCreature = mWorld->cellMap()->creatureAtAddress(soupAddr);
        return theCreature;
    }

    return NULL;
}

- (void)mouseMoved:(NSEvent*)inEvent
{
    NSPoint localPoint = [self convertPoint:[inEvent locationInWindow] fromView:nil];
    MacTierra::Creature* theCreature = [self creatureForPoint:localPoint];
    if (theCreature && !theCreature->isEmbryo())
    {
        std::string creatureName = theCreature->creatureName();
        self.focusedCreatureName = [NSString stringWithUTF8String:creatureName.c_str()];
    }
    else
    {
        self.focusedCreatureName = @"";
    }
}

- (void)mouseDown:(NSEvent*)inEvent
{
    NSPoint localPoint = [self convertPoint:[inEvent locationInWindow] fromView:nil];
    MacTierra::Creature* theCreature = [self creatureForPoint:localPoint];
    if (theCreature && !theCreature->isEmbryo())
    {
        MTCreature* creatureObj = [[[MTCreature alloc] initWithCreature:theCreature] autorelease];
//            [creatureObj genotype]; // force the genotype to be created
        mWorldController.selectedCreature = creatureObj;
        return;
    }

    mWorldController.selectedCreature = nil;
}

- (void)mouseDragged:(NSEvent*)inEvent
{
    NSPoint localPoint = [self convertPoint:[inEvent locationInWindow] fromView:nil];

    MacTierra::Creature* theCreature = [self creatureForPoint:localPoint];

    if (theCreature && !theCreature->isEmbryo())
    {
        MTCreature* creatureObj = [[[MTCreature alloc] initWithCreature:theCreature] autorelease];

        MTSerializableGenotype* serCreature = [MTSerializableGenotype serializableGenotypeFromCreature:creatureObj];

        NSPasteboard* pasteboard = [NSPasteboard pasteboardWithName:NSDragPboard];

        [pasteboard declareTypes:[NSArray arrayWithObjects:kCreatureReferencePasteboardType,
                                                           kGenotypeDataPasteboardType,
                                                           NSStringPboardType,
                                                           nil]  owner:self];

        [pasteboard setPropertyList:[creatureObj pasteboardData] forType:kCreatureReferencePasteboardType];
        [pasteboard setString:[serCreature stringRepresentation] forType:NSStringPboardType];
        [pasteboard setData:[serCreature archiveRepresentation] forType:kGenotypeDataPasteboardType];
    
        // FIXME: scale the image so that it matches the soup scaling
        NSImage* theImage = creatureObj.genotype.genotypeImage;
        
        [self dragImage:theImage
                     at:NSMakePoint(localPoint.x - [theImage size].width / 2.0, localPoint.y)
                 offset:NSZeroSize
                  event:inEvent
             pasteboard:pasteboard
                 source:self
              slideBack:YES];
    }
}

- (void)resetCursorRects
{
    [self addCursorRect:NSRectFromCGRect([self soupRect]) cursor:[NSCursor crosshairCursor]];
}

#pragma mark -

- (NSDragOperation)draggingEntered:(id <NSDraggingInfo>)sender
{
    NSDragOperation sourceDragMask = [sender draggingSourceOperationMask];

    NSPasteboard* pasteboard = [sender draggingPasteboard];
    if ([[pasteboard types] containsObject:kGenotypeDataPasteboardType])
    {
        if (sourceDragMask & NSDragOperationCopy)
            return NSDragOperationCopy;
    }
    
    return NSDragOperationNone;
}

- (NSDragOperation)draggingUpdated:(id < NSDraggingInfo >)sender
{
    return NSDragOperationCopy;
}

// perform the drop
- (BOOL)performDragOperation:(id <NSDraggingInfo>)sender
{
    NSPasteboard* pasteboard = [sender draggingPasteboard];

    BOOL inserted = NO;

    MTSerializableGenotype* genotype = [MTSerializableGenotype serializableGenotypeFromPasteboard:pasteboard];
    if (genotype)
    {
        NSUInteger creatureLen = [genotype.genome length];
        
//        NSPoint localPoint = [self convertPoint:[sender draggingLocation] fromView:nil];
//        NSPoint dragImageTopLeft = [sender draggedImageLocation];
//        dragImageTopLeft.y -= [[sender draggedImage] size].height;

        NSPoint imagePoint = [self convertPoint:[sender draggingLocation] fromView:nil];
                
        CGPoint thePoint = *(CGPoint*)&imagePoint;
        if (mWorld && [self viewPointInSoup:thePoint])
        {
            CGPoint soupPoint = [self viewPointToSoupPoint:thePoint];
            address_t soupAddr = [self soupPointToSoupAddress:soupPoint];

            if (mWorld->cellMap()->spaceAtAddress(soupAddr, creatureLen))
            {
                RefPtr<Creature> newCreature = mWorld->insertCreature(soupAddr, (const instruction_t*)[genotype.genome bytes], creatureLen);
                NSAssert(newCreature, @"Should have been able to insert");
                inserted = YES;
                [self setNeedsDisplay:YES];
            }
        }
    }
    return inserted;
}

#pragma mark -

- (NSRect)contentsRect
{
	NSRect viewBounds = [self bounds];
	NSRect imageDestRect = viewBounds;

	if (mWorld && mWorld->soup())
        imageDestRect = [self contentsRectForSize:NSMakeSize(mSoupWidth, mSoupHeight)];
    
	return imageDestRect;
}

- (BOOL)checkForGLError
{
    CGLContextObj cgl_ctx = mCGlContext;

    GLenum gl_err;
    if ((gl_err = glGetError()))
    {
        NSLog(@"OpenGL error %d", gl_err);
        return true;
    }
    return false;
}

- (void)setGLOptions
{
    CGLContextObj cgl_ctx = mCGlContext;

    GLint i;
    GLfloat redMap[256];
    GLfloat greenMap[256];
    GLfloat blueMap[256];
    
    /* define accelerated bgr233 to RGBA pixelmaps.  */
    for (i = 0; i < 256; i++)
        redMap[i] = (i & 0x7) / 7.0;

    for (i = 0; i < 256; i++)
        greenMap[i] = ((i & 0x38) >> 3) / 7.0;

    for (i = 0; i < 256; i++)
        blueMap[i] = ((i & 0xc0) >> 6) / 3.0;


    NSString* colorListPath = [[NSBundle mainBundle] pathForResource:@"Instructions0" ofType:@"clr"];
    NSColorList* colorList = [[[NSColorList alloc] initWithName:@"Instructions" fromFile:colorListPath] autorelease];
    NSColorSpace* rgbColorSpace = [NSColorSpace genericRGBColorSpace];
    if (colorList)
    {
        NSArray*    colorKeys = [colorList allKeys];
        NSUInteger n, numColors = [colorKeys count];
        for (n = 0; n < numColors; ++n)
        {
            NSString* curKey = [colorKeys objectAtIndex:n];
            // make sure we can get RGB
            NSColor* curColor = [[colorList colorWithKey:curKey] colorUsingColorSpace:rgbColorSpace];
            if (curColor)
            {
                CGFloat red, green, blue, alpha;
                [curColor getRed:&red green:&green blue:&blue alpha:&alpha];
                redMap[n] = red;
                greenMap[n] = green;
                blueMap[n] = blue;
            }
        }
    }

    glPixelTransferf(GL_ALPHA_SCALE, 0.0);
    glPixelTransferf(GL_ALPHA_BIAS,  1.0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glPixelMapfv(GL_PIXEL_MAP_I_TO_R, 256, redMap);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_G, 256, greenMap);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_B, 256, blueMap);

    GLfloat constantAlpha = 1.0;
    glPixelMapfv(GL_PIXEL_MAP_I_TO_A, 1, &constantAlpha);

    glPixelTransferi(GL_INDEX_SHIFT, 0);
    glPixelTransferi(GL_INDEX_OFFSET, 0);
    glPixelTransferi(GL_MAP_COLOR, GL_TRUE);
    glDisable(GL_DITHER);

    [self checkForGLError];

    glClearColor(0.3, 0.3, 0.3, 1.0);
}

- (void)render
{
    CGLContextObj cgl_ctx = mCGlContext;

    glClear(GL_COLOR_BUFFER_BIT);

    if (!mWorld || !mWorld->soup())
        return;

    if (zoomToFit)
    {
        glPixelZoom((GLfloat)mGlWidth / mSoupWidth, -(GLfloat)mGlHeight / mSoupHeight);
        glRasterPos2f(0, mGlHeight);
    }
    else
    {
        glPixelZoom(1, -1);
        glRasterPos2f((mGlWidth - (float)mSoupWidth) / 2.0, mSoupHeight - (((float)mSoupHeight - mGlHeight) / 2.0));
    }
    glDrawPixels(mSoupWidth, mSoupHeight, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, mWorld->soup()->soup());

    [self checkForGLError];
}

#pragma mark -

- (void)startObservingSelectedGenotypes
{
    [mGenotypesArrayController addObserver:self
                                   forKeyPath:@"selection"
                                      options:0
                                      context:NULL];
}

- (void)stopObservingSelectedGenotypes
{
    [mGenotypesArrayController removeObserver:self forKeyPath:@"selection"];
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context
{
    if ([keyPath isEqualToString:@"selection"])
    {
        [self setNeedsDisplay:YES];
    }
    else
    {
        [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
    }
}


@end
