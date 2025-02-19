/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"
#include "lang.h"

#include "mediaview.h"
#include "mainwidget.h"
#include "window.h"
#include "application.h"
#include "gui/filedialog.h"

MediaView::MediaView() : TWidget(App::wnd()),
_photo(0), _doc(0), _leftNavVisible(false), _rightNavVisible(false), _animStarted(getms()), _maxWidth(0), _maxHeight(0), _width(0),
_x(0), _y(0), _w(0), _h(0), _xStart(0), _yStart(0), _zoom(0), _pressed(false), _dragging(0), _full(-1),
_history(0), _peer(0), _user(0), _from(0), _index(-1), _msgid(0), _loadRequest(0), _over(OverNone), _down(OverNone), _lastAction(-st::medviewDeltaFromLastAction, -st::medviewDeltaFromLastAction),
_close(this, lang(lng_mediaview_close), st::medviewButton),
_save(this, lang(lng_mediaview_save), st::medviewButton),
_forward(this, lang(lng_mediaview_forward), st::medviewButton),
_delete(this, lang(lng_mediaview_delete), st::medviewButton),
_menu(0), _receiveMouse(true), _touchPress(false), _touchMove(false), _touchRightButton(false) {
	setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::Tool | Qt::NoDropShadowWindowHint);
	moveToScreen();
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);
	setMouseTracking(true);
	hide();

	connect(&_close, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(&_save, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_forward, SIGNAL(clicked()), this, SLOT(onForward()));
	connect(&_delete, SIGNAL(clicked()), this, SLOT(onDelete()));

	connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onCheckActive()));

	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));
}

void MediaView::moveToScreen() {
	QPoint wndCenter(App::wnd()->x() + App::wnd()->width() / 2, App::wnd()->y() + App::wnd()->height() / 2);
	QRect geom = QDesktopWidget().screenGeometry(wndCenter);
	_avail = QDesktopWidget().availableGeometry(wndCenter);
	if (geom != geometry()) {
		setGeometry(geom);
	}
	if (!geom.contains(_avail)) {
		_avail = geom;
	}
	_avail.moveTo(_avail.x() - geom.x(), _avail.y() - geom.y());
	_maxWidth = _avail.width() - 2 * st::medviewNavBarWidth;
	_maxHeight = _avail.height() - st::medviewTopSkip - st::medviewBottomSkip;
	_leftNav = QRect(0, 0, st::medviewNavBarWidth, height());
	_rightNav = QRect(width() - st::medviewNavBarWidth, 0, st::medviewNavBarWidth, height());

	int32 w = st::medviewMainWidth + (st::medviewTopSkip - _save.height()), l = _avail.x() + (_avail.width() - w) / 2;
	_topActions = QRect(l, _avail.y(), w, st::medviewTopSkip);
	_bottomActions = QRect(l, _avail.y() + _avail.height() - st::medviewBottomSkip, w, st::medviewBottomSkip);

	_close.move(_avail.x() + (_avail.width() + st::medviewMainWidth) / 2 - _close.width(), _avail.y() + (st::medviewTopSkip - _close.height()) / 2);
	_save.move(_avail.x() + (_avail.width() - st::medviewMainWidth) / 2, _avail.y() + (st::medviewTopSkip - _save.height()) / 2);
	_delete.move(_avail.x() + (_avail.width() + st::medviewMainWidth) / 2 - _delete.width(), _avail.y() + _avail.height() - (st::medviewTopSkip + _delete.height()) / 2);
	_forward.move(_avail.x() + (_avail.width() - st::medviewMainWidth) / 2, _avail.y() + _avail.height() - (st::medviewTopSkip + _forward.height()) / 2);
}

void MediaView::mediaOverviewUpdated(PeerData *peer) {
	if (!_photo) return;
	if (_history && _history->peer == peer) {
		_index = -1;
		for (int i = 0, l = _history->_overview[OverviewPhotos].size(); i < l; ++i) {
			if (_history->_overview[OverviewPhotos].at(i) == _msgid) {
				_index = i;
				break;
			}
		}
		updateControls();
		preloadPhotos(0);
	} else if (_user == peer) {
		_index = -1;
		for (int i = 0, l = _user->photos.size(); i < l; ++i) {
			if (_user->photos.at(i) == _photo) {
				_index = i;
				break;
			}
		}
		updateControls();
		preloadPhotos(0);
	}
}

void MediaView::changingMsgId(HistoryItem *row, MsgId newId) {
	if (row->id == _msgid) {
		_msgid = newId;
	}
	mediaOverviewUpdated(row->history()->peer);
}

void MediaView::updateControls() {
	if (!_photo && !_doc) return;

	_close.show();
	if ((_photo && _photo->full->loaded()) || (_doc && !_doc->already(true).isEmpty())) {
		_save.show();
	} else {
		_save.hide();
	}
	if (_history) {
		HistoryItem *item = App::histItemById(_msgid);
		if (dynamic_cast<HistoryMessage*>(item)) {
			_forward.show();
		} else {
			_forward.hide();
		}
		_delete.show();
	} else {
		_forward.hide();
		if (_photo && ((App::self() && App::self()->photoId == _photo->id) || (_photo->chat && _photo->chat->photoId == _photo->id))) {
			_delete.show();
		} else {
			_delete.hide();
		}
	}
	QDateTime d(date(_photo ? _photo->date : _doc->date)), dNow(date(unixtime()));
	if (d.date() == dNow.date()) {
		_dateText = lang(lng_status_lastseen_today).replace(qsl("{time}"), d.time().toString(qsl("hh:mm")));
	} else if (d.date().addDays(1) == dNow.date()) {
		_dateText = lang(lng_status_lastseen_yesterday).replace(qsl("{time}"), d.time().toString(qsl("hh:mm")));
	} else {
		_dateText = lang(lng_status_lastseen_date_time).replace(qsl("{date}"), d.date().toString(qsl("dd.MM.yy"))).replace(qsl("{time}"), d.time().toString(qsl("hh:mm")));
	}
	int32 nameWidth = _from->nameText.maxWidth(), maxWidth = _delete.x() - _forward.x() - _forward.width(), dateWidth = st::medviewDateFont->m.width(_dateText);
	if (nameWidth > maxWidth) {
		nameWidth = maxWidth;
	}
	_nameNav = QRect(_forward.x() + _forward.width() + (maxWidth - nameWidth) / 2, _forward.y() + st::medviewNameTop, nameWidth, st::msgNameFont->height);
	_dateNav = QRect(_forward.x() + _forward.width() + (maxWidth - dateWidth) / 2, _forward.y() + st::medviewDateTop, dateWidth, st::medviewDateFont->height);
	updateHeader();
	_leftNavVisible = _photo && (_index > 0 || (_index == 0 && _history && _history->_overview[OverviewPhotos].size() < _history->_overviewCount[OverviewPhotos]));
	_rightNavVisible = _photo && (_index >= 0 && (
		(_history && _index + 1 < _history->_overview[OverviewPhotos].size()) ||
		(_user && (_index + 1 < _user->photos.size() || _index + 1 < _user->photosCount))));
	updateOver(mapFromGlobal(QCursor::pos()));
	update();
}

bool MediaView::animStep(float64 msp) {
	uint64 ms = getms();
	for (Showing::iterator i = _animations.begin(); i != _animations.end();) {
		int64 start = i.value();
		switch (i.key()) {
		case OverLeftNav: update(_leftNav); break;
		case OverRightNav: update(_rightNav); break;
		case OverName: update(_nameNav); break;
		case OverDate: update(_dateNav); break;
		default: break;
		}
		float64 dt = float64(ms - start) / st::medviewButton.duration;
		if (dt >= 1) {
			_animOpacities.remove(i.key());
			i = _animations.erase(i);
		} else {
			_animOpacities[i.key()].update(dt, anim::linear);
			++i;
		}
	}
	return !_animations.isEmpty();
}

MediaView::~MediaView() {
	delete _menu;
}

void MediaView::onClose() {
	if (App::wnd()) App::wnd()->layerHidden();
}

void MediaView::onSave() {
	if (_doc) {
		QString cur = _doc->already(true), file;
		if (cur.isEmpty()) {
			_save.hide();
			return;
		}
		if (filedialogGetSaveFile(file, lang(lng_save_photo), qsl("JPEG Image (*.jpg);;All files (*.*)"), cur)) {
			if (!file.isEmpty() && file != cur) {
				QFile(cur).copy(file);
			}
		}
	} else {
		if (!_photo || !_photo->full->loaded()) return;

		QString file;
		if (filedialogGetSaveFile(file, lang(lng_save_photo), qsl("JPEG Image (*.jpg);;All files (*.*)"), filedialogDefaultName(qsl("photo"), qsl(".jpg")))) {
			if (!file.isEmpty()) {
				_photo->full->pix().toImage().save(file, "JPG");
			}
		}
	}
}

void MediaView::onShowInFolder() {
	QString already(_doc->already(true));
	if (!already.isEmpty()) psShowInFolder(already);
}

void MediaView::onForward() {
	HistoryItem *item = App::histItemById(_msgid);
	if (!_msgid || !item) return;

	if (App::wnd()) {
		onClose();
		if (App::main()) {
			App::contextItem(item);
			App::main()->forwardLayer();
		}
	}
}

void MediaView::onDelete() {
	onClose();
	if (!_msgid) {
		if (App::self() && _photo && App::self()->photoId == _photo->id) {
			App::app()->peerClearPhoto(App::self()->id);
		} else if (_photo->chat && _photo->chat->photoId == _photo->id) {
			App::app()->peerClearPhoto(_photo->chat->id);
		}
	} else {
		HistoryItem *item = App::histItemById(_msgid);
		if (item) {
			App::contextItem(item);
			App::main()->deleteLayer();
		}
	}
}

void MediaView::onCopy() {
	if (_doc) {
		QApplication::clipboard()->setPixmap(_current);
	} else {
		if (!_photo || !_photo->full->loaded()) return;

		QApplication::clipboard()->setPixmap(_photo->full->pix());
	}
}

void MediaView::showPhoto(PhotoData *photo, HistoryItem *context) {
	_history = context ? context->history() : 0;
	_peer = 0;
	_user = 0;

	_loadRequest = 0;
	_over = OverNone;
	_pressed = false;
	_dragging = 0;
	setCursor(style::cur_default);
	if (!_animations.isEmpty()) {
		_animations.clear();
		anim::stop(this);
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();
	setCursor(style::cur_default);

	_index = -1;
	_msgid = context ? context->id : 0;
	if (_history) {
		for (int i = 0, l = _history->_overview[OverviewPhotos].size(); i < l; ++i) {
			if (_history->_overview[OverviewPhotos].at(i) == _msgid) {
				_index = i;
				break;
			}
		}

		if (_history->_overviewCount[OverviewPhotos] < 0) {
			loadPhotosBack();
		}
	}

	showPhoto(photo);
	preloadPhotos(0);
}

void MediaView::showPhoto(PhotoData *photo, PeerData *context) {
	_history = 0;
	_peer = context;
	_user = context->chat ? 0 : context->asUser();

	_loadRequest = 0;
	_over = OverNone;
	if (!_animations.isEmpty()) {
		_animations.clear();
		anim::stop(this);
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();
	setCursor(style::cur_default);

	_msgid = 0;
	_index = -1;
	if (_user) {
		if (_user->photos.isEmpty() && _user->photosCount < 0 && _user->photoId) {
			_index = 0;
		}
		for (int i = 0, l = _user->photos.size(); i < l; ++i) {
			if (_user->photos.at(i) == photo) {
				_index = i;
				break;
			}
		}

		if (_user->photosCount < 0) {
			loadPhotosBack();
		}
	}
	showPhoto(photo);
	preloadPhotos(0);
}

void MediaView::showDocument(DocumentData *doc, QPixmap pix, HistoryItem *context) {
	_photo = 0;
	_history = context ? context->history() : 0;
	_peer = 0;
	_user = 0;
	_zoom = 0;
	_msgid = context ? context->id : 0;
	_index = -1;
	_loadRequest = 0;
	_over = OverNone;
	_pressed = false;
	_dragging = 0;
	setCursor(style::cur_default);
	if (!_animations.isEmpty()) {
		_animations.clear();
		anim::stop(this);
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();
	setCursor(style::cur_default);

	QString name = doc->already();
	_current = pix;
	_current.setDevicePixelRatio(cRetinaFactor());
	_doc = doc;
	_down = OverNone;
	if (isHidden()) {
		moveToScreen();
	}
	_w = _current.width() / cIntRetinaFactor();
	_h = _current.height() / cIntRetinaFactor();
	_x = _avail.x() + (_avail.width() - _w) / 2;
	_y = _avail.y() + (_avail.height() - _h) / 2;
	_width = _w;
	_from = App::user(_doc->user);
	_full = 1;
	updateControls();
	if (isHidden()) {
		psUpdateOverlayed(this);
		show();
	}
}

void MediaView::showPhoto(PhotoData *photo) {
	_photo = photo;
	_doc = 0;
	_zoom = 0;
	MTP::clearLoaderPriorities();
	_photo->full->load();
	_full = -1;
	_current = QPixmap();
	_down = OverNone;
	_w = photo->full->width();
	_h = photo->full->height();
	switch (cScale()) {
	case dbisOneAndQuarter: _w = qRound(float64(_w) * 1.25 - 0.01); _h = qRound(float64(_h) * 1.25 - 0.01); break;
	case dbisOneAndHalf: _w = qRound(float64(_w) * 1.5 - 0.01); _h = qRound(float64(_h) * 1.5 - 0.01); break;
	case dbisTwo: _w *= 2; _h *= 2; break;
	}
	if (isHidden()) {
		moveToScreen();
	}
	if (_w > _maxWidth) {
		_h = qRound(_h * _maxWidth / float64(_w));
		_w = _maxWidth;
	}
	if (_h > _maxHeight) {
		_w = qRound(_w * _maxHeight / float64(_h));
		_h = _maxHeight;
	}
	_x = _avail.x() + (_avail.width() - _w) / 2;
	_y = _avail.y() + (_avail.height() - _h) / 2;
	_width = _w;
	_from = App::user(_photo->user);
	updateControls();
	if (isHidden()) {
		psUpdateOverlayed(this);
		show();
	}
}

void MediaView::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	QRect r(e->rect());
	
	QPainter::CompositionMode m = p.compositionMode();
	p.setCompositionMode(QPainter::CompositionMode_Source);

	// main bg
	p.setOpacity(st::medviewLightOpacity);
	QRect r_bg(st::medviewNavBarWidth, 0, width() - 2 * st::medviewNavBarWidth, height());
	if (r_bg.intersects(r)) p.fillRect(r_bg.intersected(r), st::black->b);

	// left nav bar bg
	if (_leftNav.intersects(r)) {
		if (_leftNavVisible) {
			float64 o = overLevel(OverLeftNav);
			p.setOpacity(o * st::medviewDarkOpacity + (1 - o) * st::medviewLightOpacity);
			p.fillRect(_leftNav.intersected(r), st::black->b);
		} else {
			p.setOpacity(st::medviewLightOpacity);
			p.fillRect(_leftNav.intersected(r), st::black->b);
		}
	}

	// right nav bar
	if (_rightNav.intersects(r)) {
		if (_rightNavVisible) {
			float64 o = overLevel(OverRightNav);
			p.setOpacity(o * st::medviewDarkOpacity + (1 - o) * st::medviewLightOpacity);
			p.fillRect(_rightNav.intersected(r), st::black->b);
		} else {
			p.setOpacity(st::medviewLightOpacity);
			p.fillRect(_rightNav.intersected(r), st::black->b);
		}
	}

	p.setCompositionMode(m);
	p.setOpacity(1);

	// photo
	if (_photo) {
		if (_full <= 0 && _photo->full->loaded()) {
			_current = _photo->full->pixNoCache(_width * cIntRetinaFactor(), 0, true);
			if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
			_full = 1;
		} else if (_full < 0 && _photo->medium->loaded()) {
			_current = _photo->medium->pixBlurredNoCache(_width * cIntRetinaFactor());
			if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
			_full = 0;
		} else if (_current.isNull() && _photo->thumb->loaded()) {
			_current = _photo->thumb->pixBlurredNoCache(_width * cIntRetinaFactor());
			if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
		}
	}
	if (_photo || !_current.isNull()) {
		QRect imgRect(_x, _y, _w, _h);
		if (imgRect.intersects(r)) {
			if (_zoom) {
				bool was = (p.renderHints() & QPainter::SmoothPixmapTransform);
				if (!was) p.setRenderHint(QPainter::SmoothPixmapTransform);
				p.drawPixmap(QRect(_x, _y, _w, _h), _current);
				if (!was) p.setRenderHint(QPainter::SmoothPixmapTransform, false);
			} else {
				p.drawPixmap(_x, _y, _current);
			}
			if (imgRect.intersects(_topActions)) {
				p.setOpacity(st::medviewControlsBgOpacity);
				p.fillRect(imgRect.intersected(_topActions), st::black->b);
				p.setOpacity(1);
			}
			if (imgRect.intersects(_bottomActions)) {
				p.setOpacity(st::medviewControlsBgOpacity);
				p.fillRect(imgRect.intersected(_bottomActions), st::black->b);
				p.setOpacity(1);
			}
			if (_leftNavVisible && imgRect.intersects(_leftNav)) {
				float64 o = overLevel(OverLeftNav);
				p.setOpacity(o * st::medviewDarkOpacity + (1 - o) * st::medviewControlsBgOpacity);
				p.fillRect(imgRect.intersected(_leftNav), st::black->b);
				p.setOpacity(1);
			}
			if (_rightNavVisible && imgRect.intersects(_rightNav)) {
				float64 o = overLevel(OverRightNav);
				p.setOpacity(o * st::medviewDarkOpacity + (1 - o) * st::medviewControlsBgOpacity);
				p.fillRect(imgRect.intersected(_rightNav), st::black->b);
				p.setOpacity(1);
			}
			if (_full < 1) {
				uint64 dt = getms() - _animStarted;
				int32 cnt = int32(st::photoLoaderCnt), period = int32(st::photoLoaderPeriod), t = dt % period, delta = int32(st::photoLoaderDelta);

				int32 x = _avail.x() + (_avail.width() - st::mediaviewLoader.width()) / 2, y = _avail.y() + (_avail.height() - st::mediaviewLoader.height()) / 2;
				p.fillRect(x, y, st::mediaviewLoader.width(), st::mediaviewLoader.height(), st::photoLoaderBg->b);
				x += (st::mediaviewLoader.width() - cnt * st::mediaviewLoaderPoint.width() - (cnt - 1) * st::mediaviewLoaderSkip) / 2;
				y += (st::mediaviewLoader.height() - st::mediaviewLoaderPoint.height()) / 2;
				QColor c(st::white->c);
				QBrush b(c);
				for (int32 i = 0; i < cnt; ++i) {
					t -= delta;
					while (t < 0) t += period;

					float64 alpha = (t >= st::photoLoaderDuration1 + st::photoLoaderDuration2) ? 0 : ((t > st::photoLoaderDuration1 ? ((st::photoLoaderDuration1 + st::photoLoaderDuration2 - t) / st::photoLoaderDuration2) : (t / st::photoLoaderDuration1)));
					c.setAlphaF(st::photoLoaderAlphaMin + alpha * (1 - st::photoLoaderAlphaMin));
					b.setColor(c);
					p.fillRect(x + i * (st::mediaviewLoaderPoint.width() + st::mediaviewLoaderSkip), y, st::mediaviewLoaderPoint.width(), st::mediaviewLoaderPoint.height(), b);
				}
				QTimer::singleShot(AnimationTimerDelta, this, SLOT(updateImage()));
			}
		}
	}


	// left nav bar
	if (_leftNavVisible) {
		QPoint p_left((st::medviewNavBarWidth - st::medviewLeft.pxWidth()) / 2, (height() - st::medviewLeft.pxHeight()) / 2);
		if (QRect(p_left.x(), p_left.y(), st::medviewLeft.pxWidth(), st::medviewLeft.pxHeight()).intersects(r)) {
			float64 o = overLevel(OverLeftNav);
			p.setOpacity(o * st::medviewDarkNav + (1 - o) * st::medviewLightNav);
			p.drawPixmap(p_left, App::sprite(), st::medviewLeft);
		}
	}

	// right nav bar
	if (_rightNavVisible) {
		QPoint p_right(width() - (st::medviewNavBarWidth + st::medviewRight.pxWidth()) / 2, (height() - st::medviewRight.pxHeight()) / 2);
		if (QRect(p_right.x(), p_right.y(), st::medviewRight.pxWidth(), st::medviewRight.pxHeight()).intersects(r)) {
			float64 o = overLevel(OverRightNav);
			p.setOpacity(o * st::medviewDarkNav + (1 - o) * st::medviewLightNav);
			p.drawPixmap(p_right, App::sprite(), st::medviewRight);
		}
	}
	p.setOpacity(1);

	// header
	p.setPen(st::medviewHeaderColor->p);
	p.setFont(st::medviewHeaderFont->f);
	QRect r_header(_save.x() + _save.width(), _save.y(), _close.x() - _save.x() - _save.width(), _save.height());
	if (r_header.intersects(r)) p.drawText(r_header, _header, style::al_center);

	// name
	p.setPen(nameDateColor(overLevel(OverName)));
	if (_over == OverName) _from->nameText.replaceFont(st::msgNameFont->underline());
	if (_nameNav.intersects(r)) _from->nameText.drawElided(p, _nameNav.left(), _nameNav.top(), _nameNav.width());
	if (_over == OverName) _from->nameText.replaceFont(st::msgNameFont);

	// date
	p.setPen(nameDateColor(overLevel(OverDate)));
	p.setFont((_over == OverDate ? st::medviewDateFont->underline() : st::medviewDateFont)->f);
	if (_dateNav.intersects(r)) p.drawText(_dateNav.left(), _dateNav.top() + st::medviewDateFont->ascent, _dateText);
}

void MediaView::keyPressEvent(QKeyEvent *e) {
	if (!_menu && e->key() == Qt::Key_Escape) {
		onClose();
	} else if (e == QKeySequence::Save || e == QKeySequence::SaveAs) {
		onSave();
	} else if (e->key() == Qt::Key_Copy || (e->key() == Qt::Key_C && e->modifiers().testFlag(Qt::ControlModifier))) {
		onCopy();
	} else if (e->key() == Qt::Key_Left) {
		moveToPhoto(-1);
	} else if (e->key() == Qt::Key_Right) {
		moveToPhoto(1);
	} else if (e->modifiers().testFlag(Qt::ControlModifier) && (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal || e->key() == Qt::Key_Minus || e->key() == Qt::Key_Underscore || e->key() == Qt::Key_0)) {
		int32 newZoom = _zoom;
		if (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal) {
			if (newZoom < MaxZoomLevel) ++newZoom;
		} else if (e->key() == Qt::Key_Minus || e->key() == Qt::Key_Underscore) {
			if (newZoom > -MaxZoomLevel) --newZoom;
		} else {
			newZoom = 0;
			_x = _avail.x() - _width / 2;
			_y = _avail.y() - (_current.height() / cIntRetinaFactor()) / 2;
			if (_zoom >= 0) {
				_x *= _zoom + 1;
				_y *= _zoom + 1;
			} else {
				_x /= -_zoom + 1;
				_y /= -_zoom + 1;
			}
			_x += _avail.width() / 2;
			_y += _avail.height() / 2;
			update();
		}
		while ((newZoom < 0 && (-newZoom + 1) > _w) || (-newZoom + 1) > _h) {
			++newZoom;
		}
		if (_zoom != newZoom) {
			float64 nx, ny;
			_w = _current.width() / cIntRetinaFactor();
			_h = _current.height() / cIntRetinaFactor();
			if (_zoom >= 0) {
				nx = (_x - _avail.width() / 2.) / float64(_zoom + 1);
				ny = (_y - _avail.height() / 2.) / float64(_zoom + 1);
			} else {
				nx = (_x - _avail.width() / 2.) * float64(-_zoom + 1);
				ny = (_y - _avail.height() / 2.) * float64(-_zoom + 1);
			}
			_zoom = newZoom;
			if (_zoom > 0) {
				_w *= _zoom + 1;
				_h *= _zoom + 1;
				_x = int32(nx * (_zoom + 1) + _avail.width() / 2.);
				_y = int32(ny * (_zoom + 1) + _avail.height() / 2.);
			} else {
				_w /= (-_zoom + 1);
				_h /= (-_zoom + 1);
				_x = int32(nx / (-_zoom + 1) + _avail.width() / 2.);
				_y = int32(ny / (-_zoom + 1) + _avail.height() / 2.);
			}
			snapXY();

			update();
		}
	}
}

void MediaView::moveToPhoto(int32 delta) {
	if (_index < 0 || !_photo) return;

	int32 newIndex = _index + delta;
	if (_history) {
		if (newIndex >= 0 && newIndex < _history->_overview[OverviewPhotos].size()) {
			_index = newIndex;
			if (HistoryItem *item = App::histItemById(_history->_overview[OverviewPhotos][_index])) {
				_msgid = item->id;
				HistoryPhoto *photo = dynamic_cast<HistoryPhoto*>(item->getMedia());
				if (photo) {
					showPhoto(photo->photo());
					preloadPhotos(delta);
				}
			}
		}
		if (delta < 0 && _index < MediaOverviewStartPerPage) {
			loadPhotosBack();
		}
	} else if (_user) {
		if (newIndex >= 0 && newIndex < _user->photos.size()) {
			_index = newIndex;
			showPhoto(_user->photos[_index]);
			preloadPhotos(delta);
		}
		if (delta > 0 && _index > _user->photos.size() - MediaOverviewStartPerPage) {
			loadPhotosBack();
		}
	}
}

void MediaView::preloadPhotos(int32 delta) {
	if (_index < 0 || !_photo) return;

	int32 from = _index + (delta ? delta : -1), to = _index + (delta ? delta * MediaOverviewPreloadCount : 1), forget = _index - delta * 2;
	if (from > to) qSwap(from, to);
	if (_history) {
		for (int32 i = from; i <= to; ++i) {
			if (i >= 0 && i < _history->_overview[OverviewPhotos].size() && i != _index) {
				if (HistoryItem *item = App::histItemById(_history->_overview[OverviewPhotos][i])) {
					HistoryPhoto *photo = dynamic_cast<HistoryPhoto*>(item->getMedia());
					if (photo) {
						photo->photo()->full->load();
					}
				}
			}
		}
		if (forget >= 0 && forget < _history->_overview[OverviewPhotos].size() && forget != _index) {
			if (HistoryItem *item = App::histItemById(_history->_overview[OverviewPhotos][forget])) {
				HistoryMedia *media = item->getMedia();
				if (media && media->type() == MediaTypePhoto) {
					static_cast<HistoryPhoto*>(media)->photo()->forget();
				}
			}
		}
	} else if (_user) {
		for (int32 i = from; i <= to; ++i) {
			if (i >= 0 && i < _user->photos.size() && i != _index) {
				_user->photos[i]->thumb->load();
			}
		}
		for (int32 i = from; i <= to; ++i) {
			if (i >= 0 && i < _user->photos.size() && i != _index) {
				_user->photos[i]->full->load();
			}
		}
		if (forget >= 0 && forget < _user->photos.size() && forget != _index) {
			_user->photos[forget]->forget();
		}
	}
}

void MediaView::mousePressEvent(QMouseEvent *e) {
	updateOver(e->pos());
	if (_menu || !_receiveMouse) return;

	if (e->button() == Qt::LeftButton) {
		_down = OverNone;
		if (_over == OverLeftNav && _index >= 0) {
			moveToPhoto(-1);
			_lastAction = e->pos();
		} else if (_over == OverRightNav && _index >= 0) {
			moveToPhoto(1);
			_lastAction = e->pos();
		} else if (_over == OverName) {
			_down = OverName;
		} else if (_over == OverDate) {
			_down = OverDate;
		} else if (!_topActions.contains(e->pos()) && !_bottomActions.contains(e->pos())) {
			_pressed = true;
			_dragging = 0;
			setCursor(style::cur_default);
			_mStart = e->pos();
			_xStart = _x;
			_yStart = _y;
		}
	}
}

void MediaView::snapXY() {
	int32 xmin = _avail.x() + _avail.width() - _w - st::medviewNavBarWidth, xmax = _avail.x() + st::medviewNavBarWidth;
	int32 ymin = _avail.y() + _avail.height() - _h - st::medviewTopSkip, ymax = _avail.y() + st::medviewTopSkip;
	if (xmin > _avail.x() + ((_avail.width() - _w) / 2)) xmin = _avail.x() + ((_avail.width() - _w) / 2);
	if (xmax < _avail.x() + ((_avail.width() - _w) / 2)) xmax = _avail.x() + ((_avail.width() - _w) / 2);
	if (ymin > _avail.y() + ((_avail.height() - _h) / 2)) ymin = _avail.y() + ((_avail.height() - _h) / 2);
	if (ymax < _avail.y() + ((_avail.height() - _h) / 2)) ymax = _avail.y() + ((_avail.height() - _h) / 2);
	if (_x < xmin) _x = xmin;
	if (_x > xmax) _x = xmax;
	if (_y < ymin) _y = ymin;
	if (_y > ymax) _y = ymax;
}

void MediaView::mouseMoveEvent(QMouseEvent *e) {
	updateOver(e->pos());
	if (_lastAction.x() >= 0 && (e->pos() - _lastAction).manhattanLength() >= st::medviewDeltaFromLastAction) {
		_lastAction = QPoint(-st::medviewDeltaFromLastAction, -st::medviewDeltaFromLastAction);
	}
	if (_pressed) {
		if (!_dragging && (e->pos() - _mStart).manhattanLength() >= QApplication::startDragDistance()) {
			_dragging = QRect(_x, _y, _w, _h).contains(_mStart) ? 1 : -1;
			if (_dragging > 0) {
				if (_w > _avail.width() - 2 * st::medviewNavBarWidth || _h > _avail.height() - 2 * st::medviewTopSkip) {
					setCursor(style::cur_sizeall);
				} else {
					setCursor(style::cur_default);
				}
			}
		}
		if (_dragging > 0) {
			_x = _xStart + (e->pos() - _mStart).x();
			_y = _yStart + (e->pos() - _mStart).y();
			snapXY();
			update();
		}
	}
}

bool MediaView::updateOverState(OverState newState) {
	bool result = true;
	if (_over != newState) {
		if (_over != OverNone) {
			_animations[_over] = getms();
			ShowingOpacities::iterator i = _animOpacities.find(_over);
			if (i != _animOpacities.end()) {
				i->start(0);
			} else {
				_animOpacities.insert(_over, anim::fvalue(1, 0));
			}
			anim::start(this);
			if (newState != OverNone) update();
		} else {
			result = false;
		}
		_over = newState;
		if (newState != OverNone) {
			_animations[_over] = getms();
			ShowingOpacities::iterator i = _animOpacities.find(_over);
			if (i != _animOpacities.end()) {
				i->start(1);
			} else {
				_animOpacities.insert(_over, anim::fvalue(0, 1));
			}
			anim::start(this);
			setCursor(style::cur_pointer);
		} else {
			setCursor(style::cur_default);
		}
	}
	return result;
}

void MediaView::updateOver(const QPoint &pos) {
	if (_pressed || _dragging) return;

	if (_leftNavVisible && _leftNav.contains(pos)) {
		if (!updateOverState(OverLeftNav)) {
			update(_leftNav);
		}
	} else if (_rightNavVisible && _rightNav.contains(pos)) {
		if (!updateOverState(OverRightNav)) {
			update(_rightNav);
		}
	} else if (_nameNav.contains(pos)) {
		if (!updateOverState(OverName)) {
			update(_nameNav);
		}
	} else if (_msgid && _dateNav.contains(pos)) {
		if (!updateOverState(OverDate)) {
			update(_dateNav);
		}
	} else if (_over != OverNone) {
		if (_over == OverLeftNav) {
			update(_leftNav);
		} else if (_over == OverRightNav) {
			update(_rightNav);
		} else if (_over == OverName) {
			update(_nameNav);
		} else if (_over == OverDate) {
			update(_dateNav);
		}
		updateOverState(OverNone);
	}
}

void MediaView::mouseReleaseEvent(QMouseEvent *e) {
	updateOver(e->pos());
	if (_over == OverName && _down == OverName) {
		if (App::wnd()) {
			onClose();
			if (App::main()) App::main()->showPeerProfile(_from);
		}
	} else if (_over == OverDate && _down == OverDate && _msgid) {
		HistoryItem *item = App::histItemById(_msgid);
		if (item) {
			if (App::wnd()) {
				onClose();
				if (App::main()) App::main()->showPeer(item->history()->peer->id, _msgid, false, true);
			}
		}
	} else if (_pressed) {
		if (_dragging) {
			if (_dragging > 0) {
				_x = _xStart + (e->pos() - _mStart).x();
				_y = _yStart + (e->pos() - _mStart).y();
				snapXY();
				update();
			}
			_dragging = 0;
			setCursor(style::cur_default);
		} else if ((e->pos() - _lastAction).manhattanLength() >= st::medviewDeltaFromLastAction) {
			onClose();
		}
		_pressed = false;
	}
	_down = OverNone;
}

void MediaView::contextMenuEvent(QContextMenuEvent *e) {
	if (_photo && _photo->full->loaded() && (e->reason() != QContextMenuEvent::Mouse || QRect(_x, _y, _w, _h).contains(e->pos()))) {
		if (_menu) {
			_menu->deleteLater();
			_menu = 0;
		}
		_menu = new ContextMenu(this);
		_menu->addAction(lang(lng_context_save_image), this, SLOT(onSave()))->setEnabled(true);
		_menu->addAction(lang(lng_context_copy_image), this, SLOT(onCopy()))->setEnabled(true);
		_menu->addAction(lang(lng_context_close_image), this, SLOT(onClose()))->setEnabled(true);
		if (_msgid) {
			_menu->addAction(lang(lng_context_forward_image), this, SLOT(onForward()))->setEnabled(true);
			_menu->addAction(lang(lng_context_delete_image), this, SLOT(onDelete()))->setEnabled(true);
		} else if ((App::self() && App::self()->photoId == _photo->id) || (_photo->chat && _photo->chat->photoId == _photo->id)) {
			_menu->addAction(lang(lng_context_delete_image), this, SLOT(onDelete()))->setEnabled(true);
		}
		_menu->deleteOnHide();
		connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
		_menu->popup(e->globalPos());
		e->accept();
	} else if (_doc && (e->reason() != QContextMenuEvent::Mouse || QRect(_x, _y, _w, _h).contains(e->pos()))) {
		if (_menu) {
			_menu->deleteLater();
			_menu = 0;
		}
		_menu = new ContextMenu(this);
		if (!_doc->already(true).isEmpty()) {
			_menu->addAction(lang(cPlatform() == dbipMac ? lng_context_show_in_finder : lng_context_show_in_folder), this, SLOT(onShowInFolder()))->setEnabled(true);
		}
		_menu->addAction(lang(lng_context_save_document), this, SLOT(onSave()))->setEnabled(true);
		_menu->addAction(lang(lng_context_close_file), this, SLOT(onClose()))->setEnabled(true);
		if (_msgid) {
			_menu->addAction(lang(lng_context_forward_file), this, SLOT(onForward()))->setEnabled(true);
			_menu->addAction(lang(lng_context_delete_file), this, SLOT(onDelete()))->setEnabled(true);
		}
		_menu->deleteOnHide();
		connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
		_menu->popup(e->globalPos());
		e->accept();
	}
}

void MediaView::touchEvent(QTouchEvent *e) {
	switch (e->type()) {
	case QEvent::TouchBegin:
		if (_touchPress || e->touchPoints().isEmpty()) return;
		_touchTimer.start(QApplication::startDragTime());
		_touchPress = true;
		_touchMove = _touchRightButton = false;
		_touchStart = e->touchPoints().cbegin()->screenPos().toPoint();
		break;

	case QEvent::TouchUpdate:
		if (!_touchPress || e->touchPoints().isEmpty()) return;
		if (!_touchMove && (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchMove = true;
		}
		break;

	case QEvent::TouchEnd:
		if (!_touchPress) return;
		if (!_touchMove && App::wnd()) {
			Qt::MouseButton btn(_touchRightButton ? Qt::RightButton : Qt::LeftButton);
			QPoint mapped(mapFromGlobal(_touchStart)), winMapped(App::wnd()->mapFromGlobal(_touchStart));

			QMouseEvent pressEvent(QEvent::MouseButtonPress, mapped, winMapped, _touchStart, btn, Qt::MouseButtons(btn), Qt::KeyboardModifiers());
			pressEvent.accept();
			mousePressEvent(&pressEvent);

			QMouseEvent releaseEvent(QEvent::MouseButtonRelease, mapped, winMapped, _touchStart, btn, Qt::MouseButtons(btn), Qt::KeyboardModifiers());
			mouseReleaseEvent(&releaseEvent);

			if (_touchRightButton) {
				QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, mapped, _touchStart);
				contextMenuEvent(&contextEvent);
			}
		} else if (_touchMove) {
			if ((!_leftNavVisible || !_leftNav.contains(mapFromGlobal(_touchStart))) && (!_rightNavVisible || !_rightNav.contains(mapFromGlobal(_touchStart)))) {
				QPoint d = (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart);
				if (d.x() * d.x() > d.y() * d.y() && (d.x() > st::medviewSwipeDistance || d.x() < -st::medviewSwipeDistance)) {
					moveToPhoto(d.x() > 0 ? -1 : 1);
				}
			}
		}
		_touchTimer.stop();
		_touchPress = _touchMove = _touchRightButton = false;
		break;

	case QEvent::TouchCancel:
		_touchPress = false;
		_touchTimer.stop();
		break;
	}
}

bool MediaView::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			if (ev->type() != QEvent::TouchBegin || ev->touchPoints().isEmpty() || !childAt(mapFromGlobal(ev->touchPoints().cbegin()->screenPos().toPoint()))) {
				touchEvent(ev);
				return true;
			}
		}
	}
	return QWidget::event(e);
}

void MediaView::hide() {
	QWidget::hide();
	_close.clearState();
	_save.clearState();
	_forward.clearState();
	_delete.clearState();
}

void MediaView::onMenuDestroy(QObject *obj) {
	if (_menu == obj) {
		_menu = 0;
	}
	_receiveMouse = false;
	QTimer::singleShot(0, this, SLOT(receiveMouse()));
}

void MediaView::receiveMouse() {
	_receiveMouse = true;
}

void MediaView::onCheckActive() {
	if (App::wnd() && isVisible()) {
		if (App::wnd()->isActiveWindow()) {
			activateWindow();
			setFocus();
		}
	}
}

void MediaView::onTouchTimer() {
	_touchRightButton = true;
}

void MediaView::updateImage() {
	if (_current.isNull()) return;

	update(_x, _y, _w, _h);
}

void MediaView::loadPhotosBack() {
	if (_loadRequest || _index < 0 || !_photo) return;

	if (_history && _history->_overviewCount[OverviewPhotos] != 0) {
		if (App::main()) App::main()->loadMediaBack(_history->peer, OverviewPhotos);
	} else if (_user && _user->photosCount != 0) {
		int32 limit = (_index < MediaOverviewStartPerPage && _user->photos.size() > MediaOverviewStartPerPage) ? SearchPerPage : MediaOverviewStartPerPage;
		_loadRequest = MTP::send(MTPphotos_GetUserPhotos(_user->inputUser, MTP_int(_user->photos.size()), MTP_int(0), MTP_int(limit)), rpcDone(&MediaView::userPhotosLoaded, _user));
	}
}

void MediaView::userPhotosLoaded(UserData *u, const MTPphotos_Photos &photos, mtpRequestId req) {
	if (req == _loadRequest) {
		_loadRequest = 0;
	}

	const QVector<MTPPhoto> *v = 0;
	switch (photos.type()) {
	case mtpc_photos_photos: {
		const MTPDphotos_photos &d(photos.c_photos_photos());
		App::feedUsers(d.vusers);
		v = &d.vphotos.c_vector().v;
		u->photosCount = 0;
	} break;

	case mtpc_photos_photosSlice: {
		const MTPDphotos_photosSlice &d(photos.c_photos_photosSlice());
		App::feedUsers(d.vusers);
		u->photosCount = d.vcount.v;
		v = &d.vphotos.c_vector().v;
	} break;

	default: return;
	}

	if (v->isEmpty()) {
		u->photosCount = 0;
	}

	for (QVector<MTPPhoto>::const_iterator i = v->cbegin(), e = v->cend(); i != e; ++i) {
		PhotoData *photo = App::feedPhoto(*i);
		photo->thumb->load();
		u->photos.push_back(photo);
	}
	if (App::wnd()) App::wnd()->mediaOverviewUpdated(u);
}

void MediaView::updateHeader() {
	if (!_photo) {
		_header = lang(lng_mediaview_doc_image);
		return;
	}

	int32 index = _index, count = 0;
	if (_history) {
		count = _history->_overviewCount[OverviewPhotos] ? _history->_overviewCount[OverviewPhotos] : _history->_overview[OverviewPhotos].size();
		if (index >= 0) index += count - _history->_overview[OverviewPhotos].size();
	} else if (_user) {
		count = _user->photosCount ? _user->photosCount : _user->photos.size();
	}
	if (_index >= 0 && _index < count && count > 1) {
		_header = lang(lng_mediaview_n_of_count).replace(qsl("{n}"), QString::number(index + 1)).replace(qsl("{count}"), QString::number(count));
	} else if (_user) {
		_header = lang(lng_mediaview_profile_photo);
	} else if (_peer) {
		_header = lang(lng_mediaview_group_photo);
	} else {
		_header = lang(lng_mediaview_single_photo);
	}
}

float64 MediaView::overLevel(OverState control) {
	ShowingOpacities::const_iterator i = _animOpacities.constFind(control);
	return (i == _animOpacities.cend()) ? (_over == control ? 1 : 0) : i->current();
}

QColor MediaView::nameDateColor(float64 over) {
	float64 mover = 1 - over;
	QColor result;
	result.setRedF(over * st::medviewNameOverColor->c.redF() + mover * st::medviewNameColor->c.redF());
	result.setGreenF(over * st::medviewNameOverColor->c.greenF() + mover * st::medviewNameColor->c.greenF());
	result.setBlueF(over * st::medviewNameOverColor->c.blueF() + mover * st::medviewNameColor->c.blueF());
	result.setAlphaF(over * st::medviewNameOverColor->c.alphaF() + mover * st::medviewNameColor->c.alphaF());
	return result;
}
