#include "LogTable2Widget.h"
#include "SubmoduleMainWindow.h"
#include "MyTableWidgetDelegate.h"
#include "common/misc.h"
#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <cmath>
#include <map>
#include "ApplicationGlobal.h"
#include "RepositoryWrapper2Frame.h"

struct LogTable2Widget::Private {
    RepositoryWrapper2Frame *frame = nullptr;
};

/**
 * @brief コミットログを描画するためのdelegate
 */
class LogTable2WidgetDelegate : public MyTableWidgetDelegate {
private:
    RepositoryWrapper2Frame *frame() const
	{
		auto *w = dynamic_cast<LogTable2Widget *>(QStyledItemDelegate::parent());
		Q_ASSERT(w);
		return w->frame();
	}

	static QColor hiliteColor(QColor const &color)
	{
		int r = color.red();
		int g = color.green();
		int b = color.blue();
		r = 255 - (255 - r) / 2;
		g = 255 - (255 - g) / 2;
		b = 255 - (255 - b) / 2;
		return QColor(r, g, b);
	}

	static QColor shadowColor(QColor const &color)
	{
		return QColor(color.red() / 2, color.green() / 2, color.blue() / 2);
	}

	void drawSignatureIcon(QPainter *painter, const QStyleOptionViewItem &opt, QModelIndex const &index) const
	{
		if (!opt.widget->isEnabled()) return;

		Git::CommitItem const *commit = frame()->commitItem(index.row());
		if (commit) {
			QIcon icon = frame()->verifiedIcon(commit->signature);
			if (!icon.isNull()) {
				QRect r = opt.rect.adjusted(6, 3, 0, -3);
				int h = r.height();
				int w = h;
				int x = r.x() + r.width() - w;
				int y = r.y();
				icon.paint(painter, x, y, w, h);
			}
		}
	}

	void drawAvatar(QPainter *painter, const QStyleOptionViewItem &opt, QModelIndex const &index) const
	{
		if (!opt.widget->isEnabled()) return;

		int row = index.row();
		QIcon icon = frame()->committerIcon(row);
		if (!icon.isNull()) {
			int h = opt.rect.height();
			int w = h;
			int x = opt.rect.x() + opt.rect.width() - w;
			int y = opt.rect.y();

			painter->save();
			painter->setOpacity(0.5); // 半透明で描画
			icon.paint(painter, x, y, w, h);
			painter->restore();
		}
	}

	void drawLabels(QPainter *painter, const QStyleOptionViewItem &opt, QModelIndex const &index, QString const &current_branch) const
	{
		int row = index.row();
		QList<BranchLabel> const *labels = frame()->label(row);
		if (labels) {
			painter->save();
			painter->setRenderHint(QPainter::Antialiasing);

            //bool show = global->mainwindow->isLabelsVisible(); // ラベル透過モード
            //painter->setOpacity(show ? 1.0 : 0.0625);
            painter->setOpacity(1.0);

			const int space = 8;
			int x = opt.rect.x() + opt.rect.width() - 3;
			int x1 = x;
			int y0 = opt.rect.y();
			int y1 = y0 + opt.rect.height() - 1;
			int i = labels->size();
			while (i > 0) {
				i--;

				// ラベル
				BranchLabel const &label = labels->at(i);
				QString text = misc::abbrevBranchName(label.text + label.info);

				// 現在のブランチ名と一致するなら太字
				bool bold = false;
				if (text.startsWith(current_branch)) {
					auto c = text.utf16()[current_branch.size()];
					if (c == 0 || c == ',') {
						bold = true;
					}
				}

				// フォントの設定
				QFont font = painter->font();
				font.setBold(bold);
				painter->setFont(font);

				// ラベルの矩形
				int w = painter->fontMetrics().size(0, text).width() + space * 2; // 幅
				int x0 = x1 - w;
				QRect r(x0, y0, x1 - x0, y1 - y0);

				// ラベル枠の描画
                auto DrawLabelFrame = [&](int dx, int dy, QColor const &color){
                    painter->setBrush(color);
                    painter->drawRoundedRect(r.adjusted((int)lround(dx + 3), (int)lround(dy + 3), (int)lround(dx - 3), (int)lround(dy - 3)), 3, 3);
				};

				QColor color = BranchLabel::color(label.kind); // ラベル表面の色
				QColor hilite = hiliteColor(color); // ハイライトの色
				QColor shadow = shadowColor(color); // 陰の色

				painter->setPen(Qt::NoPen);
				DrawLabelFrame(-1, -1, hilite);
				DrawLabelFrame(1, 1, shadow);
				DrawLabelFrame(0, 0, color);

				// ラベルテキストの描画
				painter->setPen(Qt::black);
				painter->setBrush(Qt::NoBrush);
				QApplication::style()->drawItemText(painter, r.adjusted(space, 0, 0, 0), opt.displayAlignment, opt.palette, true, text);
				x1 = x0;
			}
			painter->restore();
		}
	}

public:
	explicit LogTable2WidgetDelegate(QObject *parent = Q_NULLPTR)
		: MyTableWidgetDelegate(parent)
	{
	}
	void paint(QPainter *painter, const QStyleOptionViewItem &option, QModelIndex const &index) const override
	{
		MyTableWidgetDelegate::paint(painter, option, index);

		enum {
			Graph,
			CommitId,
			Date,
			Author,
			Message,
		};

		// signatureの描画
		if (index.column() == CommitId) {
			drawSignatureIcon(painter, option, index);
		}

		// コミット日時
		if (index.column() == Date) {
			Git::CommitItem const *commit = frame()->commitItem(index.row());
			if (commit && commit->strange_date) {
				QColor color(255, 0, 0, 128);
				QRect r = option.rect.adjusted(1, 1, -1, -2);
				misc::drawFrame(painter, r.x(), r.y(), r.width(), r.height(), color, color);
			}
		}

		// avatarの描画
		if (index.column() == Author) {
			drawAvatar(painter, option, index);
		}

		// ラベルの描画
		if (index.column() == Message) {
			QString current_branch = frame()->currentBranchName();
			drawLabels(painter, option, index, current_branch);
		}
	}
};

LogTable2Widget::LogTable2Widget(QWidget *parent)
	: QTableWidget(parent)
	, m(new Private)
{
	setItemDelegate(new LogTable2WidgetDelegate(this));
}

LogTable2Widget::~LogTable2Widget()
{
    qDebug()<<"delete ~LogTable2Widget";
	delete m;
}

void LogTable2Widget::bind(RepositoryWrapper2Frame *frame)
{
    qDebug()<<"bind(RepositoryWrapper2Frame *frame";
	m->frame = frame;
    Q_ASSERT(m->frame);
}

RepositoryWrapper2Frame *LogTable2Widget::frame()
{
//	auto *mw = qobject_cast<RepositoryWrapper2Frame *>(window());
//	Q_ASSERT(mw);
//	return mw;
    //qDebug()<<"LogTable2Widget::frame";
    Q_ASSERT(m->frame);
	return m->frame;
}

void drawBranch2(QPainterPath *path, double x0, double y0, double x1, double y1, double r, bool bend_early)
{
	const double k = 0.55228475; // 三次ベジェ曲線で円を近似するための定数
	if (x0 == x1) {
		path->moveTo(x0, y0);
		path->lineTo(x1, y1);
	} else {
		double ym = bend_early ? (y0 + r) : (y1 - r);
		double h = fabs(y1 - y0);
		double w = fabs(x1 - x0);
		if (r > h / 2) r = h / 2;
		if (r > w / 2) r = w / 2;
		double s = r;
		if (x0 > x1) r = -r;
		if (y0 > y1) s = -s;

		if (0) {
			path->moveTo(x0, y0);
			path->lineTo(x0, ym - s);
			path->cubicTo(x0, ym - s + s * k, x0 + r - r * k, ym, x0 + r, ym);
			path->lineTo(x1 - r, ym);
			path->cubicTo(x1 - r + r * k, ym, x1, ym + s - s * k, x1, ym + s);
			path->lineTo(x1, y1);
		} else {
			if (bend_early) {
				path->moveTo(x0, y0);
				path->cubicTo(x0, ym, x1, ym, x1, ym + ym - y0);
				path->lineTo(x1, y1);
			} else {
				path->moveTo(x0, y0);
				path->lineTo(x0, ym + ym - y1);
				path->cubicTo(x0, ym, x1, ym, x1, y1);
			}
		}
	}
}

void LogTable2Widget::paintEvent(QPaintEvent *e)
{
	if (rowCount() < 1) return;
	qDebug()<<"LogTable2Widget::1paintEvent";
	QTableWidget::paintEvent(e);
	qDebug()<<"LogTable2Widget::2paintEvent";
	QPainter pr(viewport());
	pr.setRenderHint(QPainter::Antialiasing);
	pr.setBrush(QBrush(QColor(255, 255, 255)));

	Git::CommitItemList const *list = &frame()->getLogs();

	int indent_span = 16;

	int line_width = 2;
	int thick_line_width = 4;

	auto ItemRect = [&](int row){
		QRect r;
		QTableWidgetItem *p = item(row, 0);
		if (p) {
			r = visualItemRect(p);
		}
		return r;
	};

	auto IsAncestor = [&](Git::CommitItem const &item){
		return frame()->isAncestorCommit(item.commit_id);
	};

	auto ItemPoint = [&](int depth, QRect const &rect){
		int h = rect.height();
		double n = h / 2.0;
		double x = floor(rect.x() + n + depth * indent_span);
		double y = floor(rect.y() + n);
		return QPointF(x, y);
	};

	auto SetPen = [&](QPainter *pr, int level, bool thick){
		QColor c = frame()->color(level + 1);
		Qt::PenStyle s = Qt::SolidLine;
		pr->setPen(QPen(c, thick ? thick_line_width : line_width, s));
	};

	auto DrawLine = [&](size_t index, int itemrow){
		QRect rc1;
		if (index < list->size()) {
			Git::CommitItem const &item1 = list->at(index);
			rc1 = ItemRect(itemrow);
			QPointF pt1 = ItemPoint(item1.marker_depth, rc1);
			double halfheight = rc1.height() / 2.0;
			for (TreeLine const &line : item1.parent_lines) {
				if (line.depth >= 0) {
					QPainterPath *path = nullptr;
					Git::CommitItem const &item2 = list->at(line.index);
					QRect rc2 = ItemRect(line.index);
					if (index + 1 == (size_t)line.index || line.depth == item1.marker_depth || line.depth == item2.marker_depth) {
						QPointF pt2 = ItemPoint(line.depth, rc2);
						if (pt2.y() > 0) {
							path = new QPainterPath();
                            drawBranch2(path, pt1.x(), pt1.y(), pt2.x(), pt2.y(), halfheight, line.bend_early);
						}
					} else {
						QPointF pt3 = ItemPoint(item2.marker_depth, rc2);
						if (pt3.y() > 0) {
							path = new QPainterPath();
							QRect rc3 = ItemRect(itemrow + 1);
							QPointF pt2 = ItemPoint(line.depth, rc3);
                            drawBranch2(path, pt1.x(), pt1.y(), pt2.x(), pt2.y(), halfheight, true);
                            drawBranch2(path, pt2.x(), pt2.y(), pt3.x(), pt3.y(), halfheight, false);
						}
					}
					if (path) {
						SetPen(&pr, line.color_number, IsAncestor(item1));
						pr.drawPath(*path);
						delete path;
					}
				}
			}
		}
		return rc1.y();
	};

	auto DrawMark = [&](size_t index, int row){
		double x, y;
		y = 0;
		if (index < list->size()) {
			Git::CommitItem const &item = list->at(index);
			QRect rc = ItemRect(row);
			QPointF pt = ItemPoint(item.marker_depth, rc);
			double r = 4;
			x = pt.x() - r;
			y = pt.y() - r;
			if (item.resolved) {
				// ◯
				SetPen(&pr, item.marker_depth, IsAncestor(item));
				pr.drawEllipse((int)x, (int)y, int(r * 2), int(r * 2));
			} else {
				// ▽
				SetPen(&pr, item.marker_depth, false);
				QPainterPath path;
				path.moveTo(pt.x(), pt.y() + r);
				path.lineTo(pt.x() - r, pt.y() - r);
				path.lineTo(pt.x() + r, pt.y() - r);
				path.lineTo(pt.x(), pt.y() + r);
				pr.drawPath(path);
			}
		}
		return y;
	};

	// draw lines

	pr.setOpacity(0.5);
	pr.setBrush(Qt::NoBrush);

	for (size_t i = 0; i < list->size(); i++) {
		double y = DrawLine(i, i);
		if (y >= height()) break;
	}

	// draw marks

	pr.setOpacity(1);
	pr.setBrush(frame()->color(0));

	for (size_t i = 0; i < list->size(); i++) {
		double y = DrawMark(i, i);
		if (y >= height()) break;
	}
}

void LogTable2Widget::resizeEvent(QResizeEvent *e)
{
	frame()->updateAncestorCommitMap();
	QTableWidget::resizeEvent(e);
}

void LogTable2Widget::verticalScrollbarValueChanged(int value)
{
	(void)value;
	frame()->updateAncestorCommitMap();
}

