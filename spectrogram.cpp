#include "spectrogram.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QPainter>
#include <QPaintEvent>
#include <QRect>

#include <cstdlib>


Spectrogram::Spectrogram()
{
	sampleRate = 8000000;
	setFFTSize(1024);
	zoomLevel = 0;
	powerMax = 0.0f;
	powerMin = -50.0f;
}

Spectrogram::~Spectrogram()
{
	delete fft;
	delete inputSource;
}

QSize Spectrogram::sizeHint() const {
	return QSize(1024, 2048);
}

void Spectrogram::pickFile()
{
	QString fileName = QFileDialog::getOpenFileName(
		this, tr("Open File"), "", tr("Sample file (*.cfile *.bin);;All files (*)")
	);
	if (fileName != nullptr) {
		try {
			InputSource *newFile = new InputSource(fileName.toUtf8().constData());
			delete inputSource;
			inputSource = newFile;
			resize(fftSize, getHeight());
		} catch (std::runtime_error e) {
			// TODO: display error
			return;
		}
	}
}

template <class T> const T& clamp (const T& value, const T& min, const T& max) {
    return std::min(max, std::max(min, value));
}

void Spectrogram::paintEvent(QPaintEvent *event)
{
	QElapsedTimer timer;
	timer.start();

	QRect rect = event->rect();
	QPainter painter(this);
	painter.fillRect(rect, Qt::black);

	if (inputSource != nullptr) {
		int height = rect.height();

		float *line = (float*)malloc(fftSize * sizeof(float));

		QImage image(fftSize, height, QImage::Format_RGB32);
		for (int y = 0; y < height; y++) {
			getLine(line, rect.y() + y);
			for (int x = 0; x < fftSize; x++) {
				float powerRange = std::abs(int(powerMin - powerMax)); // Cast to remove overload ambiguity
				float normPower = (line[x] - powerMax) * -1.0f / powerRange;
				normPower = clamp(normPower, 0.0f, 1.0f);

				image.setPixel(x, y, QColor::fromHsvF(normPower * 0.83f, 1.0, 1.0 - normPower).rgba());
			}
		}

		QPixmap pixmap = QPixmap::fromImage(image);
		painter.drawPixmap(QRect(0, rect.y(), fftSize, height), pixmap);

		free(line);

		paintTimeAxis(&painter, rect);
	}

	qDebug() << "Paint: " << timer.elapsed() << "ms";
}

void Spectrogram::getLine(float *dest, int y)
{
	if (inputSource && fft) {
		fftwf_complex buffer[fftSize];
		inputSource->getSamples(buffer, y * getStride(), fftSize);

		for (int i = 0; i < fftSize; i++) {
			buffer[i][0] *= window[i];
			buffer[i][1] *= window[i];
		}

		fft->process(buffer, buffer);
		for (int i = 0; i < fftSize; i++) {
			int k = (i + fftSize / 2) % fftSize;
			float re = buffer[k][0];
			float im = buffer[k][1];
			float mag = sqrt(re * re + im * im) / fftSize;
			float magdb = 10 * log2(mag) / log2(10);
			*dest = magdb;
			dest++;
		}
	}
}

void Spectrogram::paintTimeAxis(QPainter *painter, QRect rect)
{
	// Round up for firstLine and round each to nearest linesPerGraduation
	int firstLine = ((rect.y() + linesPerGraduation - 1) / linesPerGraduation) * linesPerGraduation;
	int lastLine = ((rect.y() + rect.height()) / linesPerGraduation) * linesPerGraduation;

	painter->save();
	QPen pen(Qt::white, 1, Qt::SolidLine);
	painter->setPen(pen);
	QFontMetrics fm(painter->font());
	int textOffset = fm.ascent() / 2 - 1;
	for (int line = firstLine; line <= lastLine; line += linesPerGraduation) {
		painter->drawLine(0, line, 10, line);
		painter->drawText(12, line + textOffset, sampleToTime(lineToSample(line)));
	}
	painter->restore();
}

void Spectrogram::setSampleRate(int rate)
{
	sampleRate = rate;
	update();
}

void Spectrogram::setFFTSize(int size)
{
	fftSize = size;
	delete fft;
	fft = new FFT(fftSize);

	window.reset(new float[fftSize]);
	for (int i = 0; i < fftSize; i++) {
		window[i] = 0.5f * (1.0f - cos(Tau * i / (fftSize - 1)));
	}

	resize(fftSize, getHeight());
}

void Spectrogram::setPowerMax(int power)
{
	powerMax = power;
	update();
}

void Spectrogram::setPowerMin(int power)
{
	powerMin = power;
	update();
}

void Spectrogram::setZoomLevel(int zoom)
{
	zoomLevel = clamp(zoom, 0, (int)log2(fftSize));
	resize(fftSize, getHeight());
}

int Spectrogram::getHeight()
{
	if (!inputSource)
		return 0;

	return inputSource->getSampleCount() / getStride();
}

int Spectrogram::getStride()
{
	return fftSize / pow(2, zoomLevel);
}

off_t Spectrogram::lineToSample(int line) {
	return line * getStride();
}

QString Spectrogram::sampleToTime(off_t sample)
{
	return QString::number((float)sample / sampleRate).append("s");
}
