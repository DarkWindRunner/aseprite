/* ASE - Allegro Sprite Editor
 * Copyright (C) 2001-2011  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include "widgets/editor/moving_pixels_state.h"

#include "app.h"
#include "app/color_utils.h"
#include "base/unique_ptr.h"
#include "gfx/rect.h"
#include "gui/message.h"
#include "gui/system.h"
#include "gui/view.h"
#include "modules/editors.h"
#include "raster/mask.h"
#include "raster/sprite.h"
#include "tools/ink.h"
#include "tools/tool.h"
#include "widgets/editor/editor.h"
#include "widgets/editor/editor_customization_delegate.h"
#include "widgets/editor/pixels_movement.h"
#include "widgets/editor/standby_state.h"
#include "widgets/editor/transform_handles.h"
#include "widgets/statebar.h"

#include <allegro.h>

MovingPixelsState::MovingPixelsState(Editor* editor, Message* msg, PixelsMovement* pixelsMovement, HandleType handle)
{
  EditorCustomizationDelegate* customization = editor->getCustomizationDelegate();
  m_pixelsMovement = pixelsMovement;

  if (handle != NoHandle) {
    int u, v;
    editor->screenToEditor(msg->mouse.x, msg->mouse.y, &u, &v);
    m_pixelsMovement->catchImage(u, v, handle);

    editor->captureMouse();
  }

  // Setup mask color
  setTransparentColor(app_get_statusbar()->getTransparentColor());

  app_get_statusbar()->addListener(this);
  app_get_statusbar()->showMovePixelsOptions();
}

MovingPixelsState::~MovingPixelsState()
{
  app_get_statusbar()->removeListener(this);

  delete m_pixelsMovement;
}

bool MovingPixelsState::onBeforeChangeState(Editor* editor)
{
  ASSERT(m_pixelsMovement != NULL);

  // If we are changing to another state, we've to drop the image.
  if (m_pixelsMovement->isDragging())
    m_pixelsMovement->dropImageTemporarily();

  // Drop pixels if the user press a button outside the selection
  m_pixelsMovement->dropImage();

  editor->getDocument()->resetTransformation();

  delete m_pixelsMovement;
  m_pixelsMovement = NULL;

  editor->releaseMouse();

  app_get_statusbar()->hideMovePixelsOptions();
  return false;                 // Don't keep this state in history
}

void MovingPixelsState::onCurrentToolChange(Editor* editor)
{
  ASSERT(m_pixelsMovement != NULL);

  tools::Tool* current_tool = editor->getCurrentEditorTool();

  // If the user changed the tool when he/she is moving pixels,
  // we have to drop the pixels only if the new tool is not selection...
  if (m_pixelsMovement &&
      (!current_tool->getInk(0)->isSelection() ||
       !current_tool->getInk(1)->isSelection())) {
    // We have to drop pixels
    dropPixels(editor);
  }
}

bool MovingPixelsState::onMouseDown(Editor* editor, Message* msg)
{
  ASSERT(m_pixelsMovement != NULL);

  Decorator* decorator = static_cast<Decorator*>(editor->getDecorator());
  Document* document = editor->getDocument();

  // Transform selected pixels
  if (document->isMaskVisible() &&
      decorator->getTransformHandles(editor)) {
    TransformHandles* transfHandles = decorator->getTransformHandles(editor);

    // Get the handle covered by the mouse.
    HandleType handle = transfHandles->getHandleAtPoint(editor,
                                                        gfx::Point(msg->mouse.x, msg->mouse.y),
                                                        getTransformation(editor));

    if (handle != NoHandle) {
      // Re-catch the image
      int x, y;
      editor->screenToEditor(msg->mouse.x, msg->mouse.y, &x, &y);
      m_pixelsMovement->catchImageAgain(x, y, handle);

      editor->captureMouse();
      return true;
    }
  }

  // Start "moving pixels" loop
  if (editor->isInsideSelection() && (msg->mouse.left ||
                                      msg->mouse.right)) {
    // Re-catch the image
    int x, y;
    editor->screenToEditor(msg->mouse.x, msg->mouse.y, &x, &y);
    m_pixelsMovement->catchImageAgain(x, y, MoveHandle);

    editor->captureMouse();
    return true;
  }
  // End "moving pixels" loop
  else {
    // Drop pixels (e.g. to start drawing)
    dropPixels(editor);
  }

  // Use StandbyState implementation
  return StandbyState::onMouseDown(editor, msg);
}

bool MovingPixelsState::onMouseUp(Editor* editor, Message* msg)
{
  ASSERT(m_pixelsMovement != NULL);

  // Drop the image temporarily in this location (where the user releases the mouse)
  m_pixelsMovement->dropImageTemporarily();

  // Redraw the new pivot location.
  editor->invalidate();

  editor->releaseMouse();
  return true;
}

bool MovingPixelsState::onMouseMove(Editor* editor, Message* msg)
{
  ASSERT(m_pixelsMovement != NULL);

  // If there is a button pressed
  if (m_pixelsMovement->isDragging()) {
    // Infinite scroll
    editor->controlInfiniteScroll(msg);

    // Get the position of the mouse in the sprite
    int x, y;
    editor->screenToEditor(msg->mouse.x, msg->mouse.y, &x, &y);

    // Drag the image to that position
    gfx::Rect bounds = m_pixelsMovement->moveImage(x, y);

    // If "bounds" is empty is because the cel was not moved
    if (!bounds.isEmpty()) {
      // Redraw the extra cel in the new position
      jmouse_hide();
      editors_draw_sprite_tiled(editor->getSprite(),
                                bounds.x, bounds.y,
                                bounds.x+bounds.w-1,
                                bounds.y+bounds.h-1);
      jmouse_show();
    }
    editor->updateStatusBar();
    return true;
  }

  // Use StandbyState implementation
  return StandbyState::onMouseMove(editor, msg);
}

bool MovingPixelsState::onMouseWheel(Editor* editor, Message* msg)
{
  ASSERT(m_pixelsMovement != NULL);

  // Use StandbyState implementation
  return StandbyState::onMouseWheel(editor, msg);
}

bool MovingPixelsState::onSetCursor(Editor* editor)
{
  ASSERT(m_pixelsMovement != NULL);

  // Move selection
  if (m_pixelsMovement->isDragging()) {
    editor->hideDrawingCursor();
    jmouse_set_cursor(JI_CURSOR_MOVE);
    return true;
  }

  // Use StandbyState implementation
  return StandbyState::onSetCursor(editor);
}

bool MovingPixelsState::onKeyDown(Editor* editor, Message* msg)
{
  ASSERT(m_pixelsMovement != NULL);
  EditorCustomizationDelegate* customization = editor->getCustomizationDelegate();

  if (customization && customization->isCopySelectionKeyPressed()) {
    // If the user presses the CTRL key when he is dragging pixels (but
    // not pressing the mouse buttons).
    if (!jmouse_b(0) && m_pixelsMovement) {
      // Drop pixels (sure the user will press the mouse button to
      // start dragging a copy).
      dropPixels(editor);
    }
  }

  // Use StandbyState implementation
  return StandbyState::onKeyDown(editor, msg);
}

bool MovingPixelsState::onKeyUp(Editor* editor, Message* msg)
{
  ASSERT(m_pixelsMovement != NULL);

  // Use StandbyState implementation
  return StandbyState::onKeyUp(editor, msg);
}

bool MovingPixelsState::onUpdateStatusBar(Editor* editor)
{
  ASSERT(m_pixelsMovement != NULL);

  const gfx::Transformation& transform(getTransformation(editor));

  app_get_statusbar()->setStatusText
    (100, "Pos %d %d, Size %d %d, Angle %.1f",
     transform.bounds().x, transform.bounds().y,
     transform.bounds().w, transform.bounds().h,
     180.0 * transform.angle() / PI);

  return true;
}

void MovingPixelsState::dispose()
{
  // Never called as MovingPixelsState is removed automatically as
  // StatusBar's listener.
}

void MovingPixelsState::onChangeTransparentColor(const Color& color)
{
  setTransparentColor(color);
}

void MovingPixelsState::setTransparentColor(const Color& color)
{
  ASSERT(current_editor != NULL);
  ASSERT(m_pixelsMovement != NULL);

  Sprite* sprite = current_editor->getSprite();
  ASSERT(sprite != NULL);

  int imgtype = sprite->getImgType();
  m_pixelsMovement->setMaskColor(color_utils::color_for_image(color, imgtype));
}

void MovingPixelsState::dropPixels(Editor* editor)
{
  // Just change to default state (StandbyState generally). We'll
  // receive an onBeforeChangeState event after this call.
  editor->backToPreviousState();
}

gfx::Transformation MovingPixelsState::getTransformation(Editor* editor)
{
  return m_pixelsMovement->getTransformation();
}
